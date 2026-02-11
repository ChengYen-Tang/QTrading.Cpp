#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"
#include "Diagnostics/Trace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <memory_resource>
#include <unordered_set>
#include <utility>

using namespace QTrading;
using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
using namespace QTrading::Log;
using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
using QTrading::Log::FileLogger::FeatherV2::AccountEventType;
using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;
using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;
using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
using QTrading::Log::FileLogger::FeatherV2::OrderEventType;
using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

namespace {

std::pmr::synchronized_pool_resource& log_pool()
{
    static std::pmr::synchronized_pool_resource pool{ std::pmr::new_delete_resource() };
    return pool;
}

template <typename T, typename... Args>
std::shared_ptr<T> make_pooled(Args&&... args)
{
    auto& pool = log_pool();
    std::pmr::polymorphic_allocator<T> alloc(&pool);
    return std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
QTrading::Log::PayloadPtr make_log_payload(Args&&... args)
{
    return QTrading::Log::MakePayload<T>(&log_pool(), std::forward<Args>(args)...);
}

template <typename T, typename Alloc>
void LogBatchPooled(QTrading::Log::Logger* logger,
    QTrading::Log::Logger::ModuleId module_id,
    const std::vector<T, Alloc>& items)
{
    if (!logger || module_id == QTrading::Log::Logger::kInvalidModuleId || items.empty()) {
        return;
    }
    const auto ts = QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed);
    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.reserve(items.size());
    for (const auto& item : items) {
        payloads.emplace_back(make_log_payload<T>(item));
    }
    logger->LogBatchAt(module_id, payloads.data(), payloads.size(), ts);
}

template <typename T, typename Alloc>
void LogBatchPooled(QTrading::Log::Logger* logger,
    QTrading::Log::Logger::ModuleId module_id,
    std::vector<T, Alloc>& items)
{
    if (!logger || module_id == QTrading::Log::Logger::kInvalidModuleId || items.empty()) {
        return;
    }
    const auto ts = QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed);
    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.reserve(items.size());
    for (auto& item : items) {
        payloads.emplace_back(make_log_payload<T>(std::move(item)));
    }
    logger->LogBatchAt(module_id, payloads.data(), payloads.size(), ts);
}

uint64_t now_ms()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

double spot_inventory_value_from_positions(const std::vector<dto::Position>& positions)
{
    double value = 0.0;
    for (const auto& p : positions) {
        if (p.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot) {
            continue;
        }
        if (p.quantity <= 0.0) {
            continue;
        }
        value += (p.entry_price * p.quantity + p.unrealized_pnl);
    }
    return value;
}

int32_t ledger_from_instrument_type(QTrading::Dto::Trading::InstrumentType type)
{
    using Ledger = QTrading::Log::FileLogger::FeatherV2::AccountLedger;
    if (type == QTrading::Dto::Trading::InstrumentType::Spot) {
        return static_cast<int32_t>(Ledger::Spot);
    }
    if (type == QTrading::Dto::Trading::InstrumentType::Perp) {
        return static_cast<int32_t>(Ledger::Perp);
    }
    return static_cast<int32_t>(Ledger::Unknown);
}


std::vector<BinanceExchange::SymbolDataset> to_datasets(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv)
{
    std::vector<BinanceExchange::SymbolDataset> out;
    out.reserve(symbolCsv.size());
    for (const auto& [sym, csv] : symbolCsv) {
        // Legacy pair-based constructor is futures/perp-only by design.
        out.push_back(BinanceExchange::SymbolDataset{
            sym,
            csv,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Perp
        });
    }
    return out;
}

} // namespace

/// @brief Simulator for Binance futures exchange.
/// @details Reads multiple CSV files (one per symbol), publishes multi-symbol 1-minute bars,
///          updates positions/orders only when they change, and maintains a global timestamp.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level, uint64_t run_id)
    : BinanceExchange(to_datasets(symbolCsv), logger, std::make_shared<Account>(init_balance, vip_level), run_id) { }

BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    const Account::AccountInitConfig& account_init,
    uint64_t run_id)
    : BinanceExchange(to_datasets(symbolCsv), logger, std::make_shared<Account>(account_init), run_id) { }

BinanceExchange::BinanceExchange(
    const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level, uint64_t run_id)
    : BinanceExchange(datasets, logger, std::make_shared<Account>(init_balance, vip_level), run_id) { }

BinanceExchange::BinanceExchange(
    const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger,
    const Account::AccountInitConfig& account_init,
    uint64_t run_id)
    : BinanceExchange(datasets, logger, std::make_shared<Account>(account_init), run_id) { }

/// @brief Primary constructor with an external Account instance.
/// @param symbolCsv  Vector of (symbol, csv_file) pairs to drive market data.
/// @param logger     Shared Logger for writing account/position/order logs.
/// @param account    Shared Account simulation object.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account_engine,
    uint64_t run_id)
    : BinanceExchange(to_datasets(symbolCsv), logger, account_engine, run_id)
{
}

BinanceExchange::BinanceExchange(
    const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account_engine,
    uint64_t run_id)
    : spot(*this),
    perp(*this),
    account(*this),
    logger(logger),
    account_engine_(account_engine)
{
    if (logger) {
        account_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Account));
        position_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Position));
        order_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Order));
        account_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::AccountEvent));
        position_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::PositionEvent));
        order_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::OrderEvent));
        market_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::MarketEvent));
        funding_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::FundingEvent));
    }
    if (const char* env = std::getenv("QTRADING_DISABLE_EVENT_LOGS")) {
        if (env[0] == '1' || env[0] == 't' || env[0] == 'T' || env[0] == 'y' || env[0] == 'Y') {
            enable_event_logging_ = false;
        }
    }
    if (const char* env = std::getenv("QTRADING_ENABLE_KLINES_MAP")) {
        if (env[0] == '1' || env[0] == 't' || env[0] == 'T' || env[0] == 'y' || env[0] == 'Y') {
            enable_klines_map_ = true;
        }
    }
    if (run_id == 0) {
        run_id = now_ms();
    }
    log_ctx_.run_id = run_id;
    log_ctx_.step_seq = 0;
    log_ctx_.ts_exchange = 0;
    log_ctx_.event_seq = 0;

    // Load each CSV in parallel (safe: each MarketData instance is independent).
    std::vector<std::future<MarketData>> jobs;
    symbols_.reserve(datasets.size());
    md_.reserve(datasets.size());
    cursor_.assign(datasets.size(), 0);
    next_ts_by_symbol_.assign(datasets.size(), 0);
    has_next_ts_.assign(datasets.size(), 0);
    kline_counts_.reserve(datasets.size());
    jobs.reserve(datasets.size());

    for (const auto& ds : datasets) {
        symbols_.push_back(ds.symbol);
        if (account_engine_ && ds.instrument_type.has_value()) {
            account_engine_->set_instrument_type(ds.symbol, *ds.instrument_type);
        }
        jobs.emplace_back(std::async(std::launch::async, [sym = ds.symbol, csv = ds.kline_csv]() {
            return MarketData(sym, csv);
            }));
    }
    symbols_shared_ = std::make_shared<std::vector<std::string>>(symbols_);

    // Collect results deterministically by input order and initialize heap state.
    for (size_t i = 0; i < jobs.size(); ++i) {
        md_.push_back(jobs[i].get());

        const auto& data = md_[i];
        kline_counts_.push_back(data.get_klines_count());
        if (cursor_[i] < data.get_klines_count()) {
            const uint64_t ts = data.get_kline(cursor_[i]).Timestamp;
            next_ts_by_symbol_[i] = ts;
            has_next_ts_[i] = 1;
            next_ts_heap_.push(HeapItem{ ts, i });
        }
    }
    last_close_by_symbol_.assign(symbols_.size(), 0.0);
    last_close_ts_by_symbol_.assign(symbols_.size(), 0);
    has_last_close_.assign(symbols_.size(), 0);

    funding_md_.resize(datasets.size());
    funding_cursor_.assign(datasets.size(), 0);
    has_funding_.assign(datasets.size(), 0);
    last_funding_rate_by_symbol_.assign(datasets.size(), 0.0);
    last_funding_time_by_symbol_.assign(datasets.size(), 0);
    has_last_funding_.assign(datasets.size(), 0);
    for (size_t i = 0; i < datasets.size(); ++i) {
        const auto& ds = datasets[i];
        if (ds.funding_csv.has_value() && !ds.funding_csv->empty()) {
            funding_md_[i] = std::make_unique<FundingRateData>(ds.symbol, *ds.funding_csv);
            has_funding_[i] = 1;
        }
    }

    /// @details Create bounded channels:
    ///          - market_channel: 8-element sliding window of MultiKlineDto
    ///          - position_channel / order_channel: debounce buffers (latest snapshot is sufficient)
    market_channel = ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(8, OverflowPolicy::DropOldest);
    position_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Position>>();
    order_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Order>>();

    if (logger) {
        start_log_thread_();
    }
}

BinanceExchange::~BinanceExchange()
{
    stop_log_thread_();
}

QTrading::Dto::Account::BalanceSnapshot BinanceExchange::AccountApi::get_spot_balance() const
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->get_spot_balance();
}

QTrading::Dto::Account::BalanceSnapshot BinanceExchange::AccountApi::get_perp_balance() const
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->get_perp_balance();
}

double BinanceExchange::AccountApi::get_total_cash_balance() const
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->get_total_cash_balance();
}

bool BinanceExchange::AccountApi::transfer_spot_to_perp(double amount)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->transfer_spot_to_perp(amount);
}

bool BinanceExchange::AccountApi::transfer_perp_to_spot(double amount)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->transfer_perp_to_spot(amount);
}

void BinanceExchange::start_log_thread_()
{
    if (log_thread_.joinable()) {
        return;
    }
    log_stop_.store(false, std::memory_order_release);
    log_thread_ = std::thread([this]() { log_worker_(); });
}

void BinanceExchange::stop_log_thread_()
{
    if (!log_thread_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(log_mtx_);
        log_stop_.store(true, std::memory_order_release);
    }
    log_cv_.notify_all();
    log_thread_.join();
}

void BinanceExchange::enqueue_log_task_(LogTask&& task)
{
    if (!logger) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(log_mtx_);
        log_queue_.emplace_back(std::move(task));
    }
    log_cv_.notify_one();
}

void BinanceExchange::log_worker_()
{
    while (true) {
        LogTask task;
        {
            std::unique_lock<std::mutex> lk(log_mtx_);
            log_cv_.wait(lk, [this]() {
                return log_stop_.load(std::memory_order_acquire) || !log_queue_.empty();
                });
            if (log_stop_.load(std::memory_order_acquire) && log_queue_.empty()) {
                return;
            }
            task = std::move(log_queue_.front());
            log_queue_.pop_front();
        }

        log_status_snapshot(
            task.perp_balance,
            task.spot_balance,
            task.total_cash_balance,
            task.spot_inventory_value,
            task.positions,
            task.orders,
            task.cur_ver);
        if (enable_event_logging_) {
            log_events(task.ctx, *task.market, task.positions, task.orders, task.funding_events,
                task.perp_balance,
                task.spot_balance,
                task.total_cash_balance,
                task.spot_inventory_value,
                std::move(task.fill_events),
                task.cur_ver);
        }
    }
}

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    QTR_TRACE("ex", "step begin");

    uint64_t ts;
    /// @details Find the next minimum timestamp among all symbols.
    {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        if (!next_timestamp(ts)) {
            QTR_TRACE("ex", "no next timestamp -> close channels");
            market_channel->Close();
            position_channel->Close();
            order_channel->Close();
            return false;
        }
    }

    QTrading::Dto::Account::BalanceSnapshot perp_balance{};
    QTrading::Dto::Account::BalanceSnapshot spot_balance{};
    double total_cash_balance = 0.0;
    double spot_inventory_value = 0.0;
    std::vector<Account::FillEvent> fill_events;
    std::vector<FundingEventDto> funding_events;
    PositionSnapshotPtr curP;
    OrderSnapshotPtr curO;
    uint64_t cur_ver = 0;
    FundingApplyTiming apply_timing = FundingApplyTiming::BeforeMatching;
    QTrading::Infra::Logging::StepLogContext step_log_ctx{};
    MultiKlinePtr dto;
    std::unordered_map<std::string, KlineDto> kline_snap_cache;
    kline_snap_cache.reserve(md_.size());

    {
        std::lock_guard<std::mutex> lk(account_mtx_);
        const uint64_t next_step = processed_steps_ + 1;
        flush_deferred_orders_locked_(next_step);
        processed_steps_ = next_step;
        set_global_timestamp(ts);
        apply_timing = funding_apply_timing_;

        const auto initial_perp_balance = account_engine_->get_perp_balance();
        const auto initial_spot_balance = account_engine_->get_spot_balance();
        const bool no_positions = account_engine_->get_all_positions().empty();
        const bool no_orders = account_engine_->get_all_open_orders().empty();

        // Terminate simulation only when both ledgers are depleted and account is inactive.
        if (initial_perp_balance.WalletBalance <= 0.0 &&
            initial_spot_balance.WalletBalance <= 0.0 &&
            no_positions &&
            no_orders) {
            QTR_TRACE("ex", "balance depleted -> close channels");
            market_channel->Close();
            position_channel->Close();
            order_channel->Close();
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        last_step_ts_ = ts;
        log_ctx_.ts_exchange = ts;
        log_ctx_.step_seq += 1;
        log_ctx_.event_seq = 0;
        step_log_ctx = log_ctx_;
    }
    dto = make_pooled<MultiKlineDto>();

    auto build_market = [&]() {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        build_multikline(ts, *dto, kline_snap_cache);
    };

    auto update_positions = [&]() {
        if (kline_snap_cache.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lk(account_mtx_);
        if (account_engine_) {
            account_engine_->update_positions(kline_snap_cache);
        }
    };

    auto apply_funding = [&]() {
        std::scoped_lock<std::mutex, std::mutex> lk(account_mtx_, state_mtx_);
        collect_funding_events_unlocked_(ts, funding_events);
    };

    if (apply_timing == FundingApplyTiming::BeforeMatching) {
        apply_funding();
        build_market();
        update_positions();
    }
    else {
        build_market();
        update_positions();
        apply_funding();
    }

    {
        std::lock_guard<std::mutex> lk(account_mtx_);
        cur_ver = account_engine_->get_state_version();
        curP = make_pooled<std::vector<dto::Position>>(account_engine_->get_all_positions());
        curO = make_pooled<std::vector<dto::Order>>(account_engine_->get_all_open_orders());
        perp_balance = account_engine_->get_perp_balance();
        spot_balance = account_engine_->get_spot_balance();
        total_cash_balance = account_engine_->get_total_cash_balance();
        spot_inventory_value = curP ? spot_inventory_value_from_positions(*curP) : 0.0;
        fill_events = account_engine_->drain_fill_events();
    }

    step_log_ctx.ts_local = now_ms();

    QTR_TRACE("ex", "market_channel Send begin");
    market_channel->Send(dto);
    QTR_TRACE("ex", "market_channel Send end");

    bool pos_changed = false;
    bool ord_changed = false;

    // Only consider sending snapshots when account reported a state transition.
    if (cur_ver != last_account_version_ && curP && curO) {
        const bool pos_unchanged = last_pos_snapshot_
            ? vec_equal(*curP, *last_pos_snapshot_)
            : curP->empty();
        const bool ord_unchanged = last_ord_snapshot_
            ? vec_equal(*curO, *last_ord_snapshot_)
            : curO->empty();

        if (!pos_unchanged) {
            QTR_TRACE("ex", "position_channel Send");
            position_channel->Send(*curP);
            pos_changed = true;
        }

        if (!ord_unchanged) {
            QTR_TRACE("ex", "order_channel Send");
            order_channel->Send(*curO);
            ord_changed = true;
        }
    }

    if (logger) {
        LogTask task;
        task.ctx = step_log_ctx;
        task.market = dto;
        task.positions = curP;
        task.orders = curO;
        task.funding_events = std::move(funding_events);
        task.perp_balance = perp_balance;
        task.spot_balance = spot_balance;
        task.total_cash_balance = total_cash_balance;
        task.spot_inventory_value = spot_inventory_value;
        task.fill_events = std::move(fill_events);
        task.cur_ver = cur_ver;
        enqueue_log_task_(std::move(task));
    }

    if (cur_ver != last_account_version_) {
        if (pos_changed) {
            last_pos_snapshot_ = curP;
        }
        if (ord_changed) {
            last_ord_snapshot_ = curO;
        }
        last_account_version_ = cur_ver;
    }

    QTR_TRACE("ex", "step end");
    return true;
}

void BinanceExchange::enqueue_deferred_order_locked_(uint64_t due_step, std::function<void(Account&, uint64_t)> fn)
{
    deferred_order_commands_.push_back(DeferredOrderCommand{ due_step, std::move(fn) });
}

void BinanceExchange::flush_deferred_orders_locked_(uint64_t step_seq)
{
    if (deferred_order_commands_.empty() || !account_engine_) {
        return;
    }

    // In-place stable compaction avoids O(K^2) middle-erases on deque.
    size_t write = 0;
    for (size_t read = 0; read < deferred_order_commands_.size(); ++read) {
        auto& cmd = deferred_order_commands_[read];
        if (cmd.due_step <= step_seq) {
            cmd.fn(*account_engine_, step_seq);
            continue;
        }
        if (write != read) {
            deferred_order_commands_[write] = std::move(cmd);
        }
        ++write;
    }
    deferred_order_commands_.resize(write);
}

void BinanceExchange::push_async_order_ack_locked_(AsyncOrderAck ack)
{
    async_order_acks_.push_back(std::move(ack));
}

std::pair<int, std::string> BinanceExchange::map_binance_reject_(const std::optional<Account::OrderRejectInfo>& reject)
{
    using Code = Account::OrderRejectInfo::Code;
    if (!reject.has_value() || reject->code == Code::None) {
        return { 0, {} };
    }

    switch (reject->code) {
    case Code::InvalidQuantity:
        return { -4003, "Quantity less than zero." };
    case Code::DuplicateClientOrderId:
        return { -4111, "DUPLICATED_CLIENT_TRAN_ID" };
    case Code::StpExpiredTaker:
    case Code::StpExpiredBoth:
        return { -2010, "Order would trigger self-trade prevention." };
    case Code::SpotHedgeModeUnsupported:
    case Code::HedgeModePositionSideRequired:
        return { -4061, "Order's position side does not match user's setting." };
    case Code::StrictHedgeReduceOnlyDisabled:
    case Code::ReduceOnlyNoReduciblePosition:
        return { -2022, "ReduceOnly Order is rejected." };
    case Code::PriceFilterBelowMin:
    case Code::PriceFilterAboveMax:
    case Code::PriceFilterInvalidTick:
        return { -1013, "Filter failure: PRICE_FILTER" };
    case Code::LotSizeBelowMinQty:
    case Code::LotSizeAboveMaxQty:
    case Code::LotSizeInvalidStep:
        return { -1013, "Filter failure: LOT_SIZE" };
    case Code::NotionalNoReferencePrice:
    case Code::NotionalBelowMin:
    case Code::NotionalAboveMax:
        return { -1013, "Filter failure: NOTIONAL" };
    case Code::PercentPriceAboveBound:
    case Code::PercentPriceBelowBound:
        return { -1013, "Filter failure: PERCENT_PRICE" };
    case Code::SpotInsufficientCash:
    case Code::SpotNoInventory:
    case Code::SpotQuantityExceedsInventory:
    case Code::SpotNoLongPositionToClose:
    default:
        break;
    }

    return { -2010, "NEW_ORDER_REJECTED" };
}

/// @brief Get a snapshot of all current positions.
/// @return Const reference to the vector of Position DTOs.
const std::vector<dto::Position>& BinanceExchange::get_all_positions() const {
    return account_engine_->get_all_positions();
}

/// @brief Get a snapshot of all current open orders.
/// @return Const reference to the vector of Order DTOs.
const std::vector<dto::Order>& BinanceExchange::get_all_open_orders() const {
    return account_engine_->get_all_open_orders();
}

void BinanceExchange::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account_engine_->set_symbol_leverage(symbol, new_leverage);
}

double BinanceExchange::get_symbol_leverage(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->get_symbol_leverage(symbol);
}

QTrading::Dto::Account::BalanceSnapshot BinanceExchange::get_spot_balance() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->get_spot_balance();
}

QTrading::Dto::Account::BalanceSnapshot BinanceExchange::get_perp_balance() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->get_perp_balance();
}

double BinanceExchange::get_total_cash_balance() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->get_total_cash_balance();
}

bool BinanceExchange::transfer_spot_to_perp(double amount)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->transfer_spot_to_perp(amount);
}

bool BinanceExchange::transfer_perp_to_spot(double amount)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account_engine_->transfer_perp_to_spot(amount);
}

void BinanceExchange::set_order_latency_bars(size_t bars)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    order_latency_bars_ = bars;
}

size_t BinanceExchange::order_latency_bars() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return order_latency_bars_;
}

std::vector<BinanceExchange::AsyncOrderAck> BinanceExchange::drain_async_order_acks()
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    std::vector<AsyncOrderAck> out;
    out.swap(async_order_acks_);
    return out;
}

void BinanceExchange::set_funding_apply_timing(FundingApplyTiming timing)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    funding_apply_timing_ = timing;
}

BinanceExchange::FundingApplyTiming BinanceExchange::funding_apply_timing() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return funding_apply_timing_;
}

void BinanceExchange::set_funding_mark_price_max_age_ms(uint64_t max_age_ms)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    funding_mark_price_max_age_ms_ = max_age_ms;
}

uint64_t BinanceExchange::funding_mark_price_max_age_ms() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return funding_mark_price_max_age_ms_;
}

void BinanceExchange::set_uncertainty_band_bps(double bps)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    uncertainty_band_bps_ = std::max(0.0, bps);
}

double BinanceExchange::uncertainty_band_bps() const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return uncertainty_band_bps_;
}

/// @brief Close the simulator: drain CSVs and close all channels.
/// @details Advances each symbol's cursor to the end before closing.
void BinanceExchange::close()
{
    {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        for (size_t i = 0; i < md_.size(); ++i) {
            cursor_[i] = md_[i].get_klines_count();
            has_next_ts_[i] = 0;
        }
    }

	IExchange<MultiKlinePtr>::close();
}

/// @brief Determine the next global timestamp to emit.
/// @param[out] ts  The minimum upcoming timestamp among all symbols.
/// @return true if at least one symbol has data remaining.
bool BinanceExchange::next_timestamp(uint64_t& ts)
{
    uint64_t next_kline_ts = std::numeric_limits<uint64_t>::max();
    bool has_next_kline = false;

    // Pop stale heap entries until top matches current per-symbol next_ts.
    while (!next_ts_heap_.empty()) {
        const auto top = next_ts_heap_.top();
        if (top.sym_id < next_ts_by_symbol_.size() &&
            has_next_ts_[top.sym_id] &&
            next_ts_by_symbol_[top.sym_id] == top.ts) {
            next_kline_ts = top.ts;
            has_next_kline = true;
            break;
        }
        next_ts_heap_.pop();
    }

    uint64_t next_funding_ts = std::numeric_limits<uint64_t>::max();
    bool has_next_funding = false;
    const size_t funding_count = std::min(funding_md_.size(), funding_cursor_.size());
    for (size_t i = 0; i < funding_count; ++i) {
        if (!has_funding_[i] || !funding_md_[i]) {
            continue;
        }
        const auto& data = *funding_md_[i];
        const size_t cur = funding_cursor_[i];
        if (cur >= data.get_count()) {
            continue;
        }
        const uint64_t fts = data.get_funding(cur).FundingTime;
        if (!has_next_funding || fts < next_funding_ts) {
            next_funding_ts = fts;
            has_next_funding = true;
        }
    }

    if (!has_next_kline && !has_next_funding) {
        ts = std::numeric_limits<uint64_t>::max();
        return false;
    }

    ts = std::min(next_kline_ts, next_funding_ts);
    return true;
}

/// @brief Build and send a MultiKlineDto for timestamp `ts`.
/// @param ts   Global timestamp to align on.
/// @param out  DTO to populate with per-symbol optional KlineDto.
void BinanceExchange::build_multikline(uint64_t ts,
    MultiKlineDto& out,
    std::unordered_map<std::string, KlineDto>& kline_snap_cache)
{
    out.Timestamp = ts;
    out.symbols = symbols_shared_;
    out.klines.clear();
    if (enable_klines_map_) {
        out.klines.reserve(md_.size());
    }
    out.klines_by_id.clear();
    out.klines_by_id.resize(symbols_.size());
    out.funding_by_id.clear();
    out.funding_by_id.resize(symbols_.size());

    kline_snap_cache.clear();
    kline_snap_cache.reserve(md_.size());

    for (size_t i = 0; i < md_.size(); ++i) {
        const auto& sym = symbols_[i];
        auto& data = md_[i];
        size_t idx = cursor_[i];
        if (idx < data.get_klines_count() &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            if (enable_klines_map_) {
                out.klines.emplace(sym, k);
            }
            out.klines_by_id[i] = k;
            kline_snap_cache.emplace(sym, k);
            last_close_by_symbol_[i] = k.ClosePrice;
            last_close_ts_by_symbol_[i] = k.Timestamp;
            has_last_close_[i] = 1;
            ++cursor_[i];

            // Advance this symbol in the multiway merge heap.
            if (cursor_[i] < data.get_klines_count()) {
                const uint64_t next_ts = data.get_kline(cursor_[i]).Timestamp;
                next_ts_by_symbol_[i] = next_ts;
                has_next_ts_[i] = 1;
                next_ts_heap_.push(HeapItem{ next_ts, i });
            }
            else {
                has_next_ts_[i] = 0;
            }
        }
        else {
            if (enable_klines_map_) {
                out.klines.emplace(sym, std::nullopt);
            }
        }

        if (i < has_last_funding_.size() && has_last_funding_[i]) {
            out.funding_by_id[i] = FundingRateDto(
                last_funding_time_by_symbol_[i],
                last_funding_rate_by_symbol_[i]);
        }
    }
}

/// @brief Log account balance, positions, and orders via the Logger.
/// @details Uses Arrow Feather-V2 for efficient batch logging.
void BinanceExchange::log_status_snapshot(const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
    const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
    double total_cash_balance,
    double spot_inventory_value,
    const PositionSnapshotPtr& positions,
    const OrderSnapshotPtr& orders,
    uint64_t cur_ver)
{
    if (!logger) {
        return;
    }

    static const std::vector<dto::Position> kEmptyPositions;
    static const std::vector<dto::Order> kEmptyOrders;
    const auto& positions_ref = positions ? *positions : kEmptyPositions;
    const auto& orders_ref = orders ? *orders : kEmptyOrders;

    if (cur_ver == last_logged_version_) {
        return;
    }

    if (account_module_id_ != Logger::kInvalidModuleId) {
        const double spot_ledger_value = spot_balance.WalletBalance + spot_inventory_value;
        const double total_ledger_value = perp_balance.Equity + spot_ledger_value;
        logger->Log(account_module_id_, make_log_payload<AccountLog>(
            AccountLog{
                perp_balance.WalletBalance,
                perp_balance.UnrealizedPnl,
                perp_balance.Equity,
                perp_balance.WalletBalance,
                perp_balance.AvailableBalance,
                perp_balance.Equity,
                spot_balance.WalletBalance,
                spot_balance.AvailableBalance,
                spot_inventory_value,
                spot_ledger_value,
                total_cash_balance,
                total_ledger_value
            }));
    }
    if (position_module_id_ != Logger::kInvalidModuleId) {
        LogBatchPooled(logger.get(), position_module_id_, positions_ref);
    }
    if (order_module_id_ != Logger::kInvalidModuleId) {
        LogBatchPooled(logger.get(), order_module_id_, orders_ref);
    }

    last_logged_version_ = cur_ver;
}

void BinanceExchange::log_events(QTrading::Infra::Logging::StepLogContext ctx,
    const MultiKlineDto& market,
    const PositionSnapshotPtr& cur_positions,
    const OrderSnapshotPtr& cur_orders,
    const std::vector<FundingEventDto>& funding_events,
    const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
    const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
    double total_cash_balance,
    double spot_inventory_value,
    std::vector<Account::FillEvent>&& fill_events,
    uint64_t cur_ver)
{
    if (!logger) {
        return;
    }
    if (!enable_event_logging_) {
        return;
    }

    static const std::vector<dto::Position> kEmptyPositions;
    static const std::vector<dto::Order> kEmptyOrders;
    const auto& cur_positions_ref = cur_positions ? *cur_positions : kEmptyPositions;
    const auto& cur_orders_ref = cur_orders ? *cur_orders : kEmptyOrders;
    const auto& prev_positions_ref = last_event_pos_snapshot_ ? *last_event_pos_snapshot_ : kEmptyPositions;
    const auto& prev_orders_ref = last_event_ord_snapshot_ ? *last_event_ord_snapshot_ : kEmptyOrders;

    account_event_buffer_.reset(ctx);
    position_event_buffer_.reset(ctx);
    order_event_buffer_.reset(ctx);
    market_event_buffer_.reset(ctx);
    funding_event_buffer_.reset(ctx);

    if (market_event_module_id_ != Logger::kInvalidModuleId) {
        market_event_buffer_.reserve(symbols_.size());
        for (size_t i = 0; i < symbols_.size(); ++i) {
            const auto& sym = symbols_[i];
            MarketEventDto e;
            e.symbol = sym;
            if (i < market.klines_by_id.size() && market.klines_by_id[i].has_value()) {
                const auto& k = market.klines_by_id[i].value();
                e.has_kline = true;
                e.open = k.OpenPrice;
                e.high = k.HighPrice;
                e.low = k.LowPrice;
                e.close = k.ClosePrice;
                e.volume = k.Volume;
                e.taker_buy_base_volume = k.TakerBuyBaseVolume;
            }
            else {
                e.has_kline = false;
            }
            market_event_buffer_.push(std::move(e));
        }

        LogBatchPooled(logger.get(), market_event_module_id_, market_event_buffer_.events);
    }

    if (funding_event_module_id_ != Logger::kInvalidModuleId && !funding_events.empty()) {
        funding_event_buffer_.reserve(funding_events.size());
        for (const auto& e : funding_events) {
            FundingEventDto ev = e;
            funding_event_buffer_.push(std::move(ev));
        }
        LogBatchPooled(logger.get(), funding_event_module_id_, funding_event_buffer_.events);
    }

    if (cur_ver == last_event_version_) {
        return;
    }

    std::unordered_set<int> filled_order_ids;
    if (!fill_events.empty()) {
        filled_order_ids.reserve(fill_events.size() * 2);
    }

    if (account_event_module_id_ != Logger::kInvalidModuleId) {
        double last_wallet_for_fill = last_wallet_balance_.value_or(
            fill_events.empty() ? perp_balance.WalletBalance : fill_events.front().perp_balance_snapshot.WalletBalance);

        for (const auto& f : fill_events) {
            const auto& perp_after = f.perp_balance_snapshot;
            const auto& spot_after = f.spot_balance_snapshot;
            const double fill_spot_inventory = spot_inventory_value_from_positions(f.positions_snapshot);
            const double fill_spot_ledger = spot_after.WalletBalance + fill_spot_inventory;
            const double fill_total_ledger = perp_after.Equity + fill_spot_ledger;

            AccountEventDto e;
            e.request_id = static_cast<uint64_t>(f.order_id);
            e.source_order_id = f.order_id;
            e.symbol = f.symbol;
            e.instrument_type = static_cast<int32_t>(f.instrument_type);
            e.ledger = ledger_from_instrument_type(f.instrument_type);
            e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
            e.wallet_delta = perp_after.WalletBalance - last_wallet_for_fill;
            e.wallet_balance_after = perp_after.WalletBalance;
            e.margin_balance_after = perp_after.MarginBalance;
            e.available_balance_after = perp_after.AvailableBalance;
            e.perp_wallet_balance_after = perp_after.WalletBalance;
            e.perp_margin_balance_after = perp_after.MarginBalance;
            e.perp_available_balance_after = perp_after.AvailableBalance;
            e.spot_wallet_balance_after = spot_after.WalletBalance;
            e.spot_available_balance_after = spot_after.AvailableBalance;
            e.spot_inventory_value_after = fill_spot_inventory;
            e.spot_ledger_value_after = fill_spot_ledger;
            e.total_cash_balance_after = f.total_cash_balance_snapshot;
            e.total_ledger_value_after = fill_total_ledger;
            account_event_buffer_.push(std::move(e));
            last_wallet_for_fill = perp_after.WalletBalance;
        }

        const double final_spot_ledger = spot_balance.WalletBalance + spot_inventory_value;
        const double final_total_ledger = perp_balance.Equity + final_spot_ledger;

        AccountEventDto e;
        e.request_id = 0;
        e.source_order_id = -1;
        e.symbol = "";
        e.instrument_type = -1;
        e.ledger = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::AccountLedger::Both);
        e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
        e.wallet_delta = perp_balance.WalletBalance - last_wallet_for_fill;
        e.wallet_balance_after = perp_balance.WalletBalance;
        e.margin_balance_after = perp_balance.MarginBalance;
        e.available_balance_after = perp_balance.AvailableBalance;
        e.perp_wallet_balance_after = perp_balance.WalletBalance;
        e.perp_margin_balance_after = perp_balance.MarginBalance;
        e.perp_available_balance_after = perp_balance.AvailableBalance;
        e.spot_wallet_balance_after = spot_balance.WalletBalance;
        e.spot_available_balance_after = spot_balance.AvailableBalance;
        e.spot_inventory_value_after = spot_inventory_value;
        e.spot_ledger_value_after = final_spot_ledger;
        e.total_cash_balance_after = total_cash_balance;
        e.total_ledger_value_after = final_total_ledger;
        account_event_buffer_.push(std::move(e));

        LogBatchPooled(logger.get(), account_event_module_id_, account_event_buffer_.events);
    }
    last_wallet_balance_ = perp_balance.WalletBalance;

    const bool need_position_events = position_event_module_id_ != Logger::kInvalidModuleId;
    const bool need_order_events = order_event_module_id_ != Logger::kInvalidModuleId;
    if (need_position_events || need_order_events) {
        std::unordered_map<int, const dto::Position*> prev_pos_by_id;
        prev_pos_by_id.reserve(prev_positions_ref.size());
        for (const auto& p : prev_positions_ref) {
            prev_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_map<int, const dto::Position*> cur_pos_by_id;
        cur_pos_by_id.reserve(cur_positions_ref.size());
        for (const auto& p : cur_positions_ref) {
            cur_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_set<int> changed_position_ids;
        changed_position_ids.reserve(cur_positions_ref.size() + prev_positions_ref.size());

        auto push_position_event = [&](const dto::Position& p, PositionEventType type) {
            PositionEventDto e;
            e.request_id = static_cast<uint64_t>(p.order_id);
            e.source_order_id = p.order_id;
            e.position_id = p.id;
            e.symbol = p.symbol;
            e.instrument_type = static_cast<int32_t>(p.instrument_type);
            e.is_long = p.is_long;
            e.event_type = static_cast<int32_t>(type);
            e.qty = p.quantity;
            e.entry_price = p.entry_price;
            e.notional = p.notional;
            e.unrealized_pnl = p.unrealized_pnl;
            e.initial_margin = p.initial_margin;
            e.maintenance_margin = p.maintenance_margin;
            e.leverage = p.leverage;
            e.fee = p.fee;
            e.fee_rate = p.fee_rate;
            position_event_buffer_.push(std::move(e));
        };

        constexpr double kQtyEps = 1e-8;
        if (need_position_events) {
            for (const auto& f : fill_events) {
                for (const auto& p : f.positions_snapshot) {
                    PositionEventDto e;
                    e.request_id = static_cast<uint64_t>(f.order_id);
                    e.source_order_id = f.order_id;
                    e.position_id = p.id;
                    e.symbol = p.symbol;
                    e.instrument_type = static_cast<int32_t>(p.instrument_type);
                    e.is_long = p.is_long;
                    e.event_type = static_cast<int32_t>(PositionEventType::Snapshot);
                    e.qty = p.quantity;
                    e.entry_price = p.entry_price;
                    e.notional = p.notional;
                    e.unrealized_pnl = p.unrealized_pnl;
                    e.initial_margin = p.initial_margin;
                    e.maintenance_margin = p.maintenance_margin;
                    e.leverage = p.leverage;
                    e.fee = p.fee;
                    e.fee_rate = p.fee_rate;
                    position_event_buffer_.push(std::move(e));
                }
            }

            for (const auto& kv : cur_pos_by_id) {
                const auto& cur = *kv.second;
                auto it = prev_pos_by_id.find(kv.first);
                if (it == prev_pos_by_id.end()) {
                    push_position_event(cur, PositionEventType::Opened);
                    changed_position_ids.insert(cur.id);
                    continue;
                }
                const auto& prev = *it->second;
                const double delta_qty = cur.quantity - prev.quantity;
                if (std::abs(delta_qty) > kQtyEps) {
                    auto type = delta_qty > 0.0 ? PositionEventType::Increased : PositionEventType::Reduced;
                    push_position_event(cur, type);
                    changed_position_ids.insert(cur.id);
                }
            }

            for (const auto& kv : prev_pos_by_id) {
                if (cur_pos_by_id.find(kv.first) == cur_pos_by_id.end()) {
                    push_position_event(*kv.second, PositionEventType::Closed);
                    changed_position_ids.insert(kv.first);
                }
            }

            LogBatchPooled(logger.get(), position_event_module_id_, position_event_buffer_.events);
        }
        else {
            for (const auto& kv : cur_pos_by_id) {
                auto it = prev_pos_by_id.find(kv.first);
                if (it == prev_pos_by_id.end()) {
                    changed_position_ids.insert(kv.first);
                    continue;
                }
                const auto& cur = *kv.second;
                const auto& prev = *it->second;
                if (std::abs(cur.quantity - prev.quantity) > kQtyEps) {
                    changed_position_ids.insert(kv.first);
                }
            }
            for (const auto& kv : prev_pos_by_id) {
                if (cur_pos_by_id.find(kv.first) == cur_pos_by_id.end()) {
                    changed_position_ids.insert(kv.first);
                }
            }
        }

        if (need_order_events) {
            std::unordered_map<int, const dto::Order*> prev_ord_by_id;
            prev_ord_by_id.reserve(prev_orders_ref.size());
            for (const auto& o : prev_orders_ref) {
                prev_ord_by_id.emplace(o.id, &o);
            }

            std::unordered_map<int, const dto::Order*> cur_ord_by_id;
            cur_ord_by_id.reserve(cur_orders_ref.size());
            for (const auto& o : cur_orders_ref) {
                cur_ord_by_id.emplace(o.id, &o);
            }

            auto push_order_event = [&](const dto::Order& o, OrderEventType type,
                double exec_qty, double exec_price, double remaining_qty) {
                OrderEventDto e;
                e.request_id = static_cast<uint64_t>(o.id);
                e.order_id = o.id;
                e.symbol = o.symbol;
                e.instrument_type = static_cast<int32_t>(o.instrument_type);
                e.event_type = static_cast<int32_t>(type);
                e.side = static_cast<int32_t>(o.side);
                e.position_side = static_cast<int32_t>(o.position_side);
                e.reduce_only = o.reduce_only;
                e.qty = o.quantity;
                e.price = o.price;
                e.exec_qty = exec_qty;
                e.exec_price = exec_price;
                e.remaining_qty = remaining_qty;
                e.closing_position_id = o.closing_position_id;
                e.is_taker = false;
                e.fee = 0.0;
                e.fee_rate = 0.0;
                e.reject_reason = 0;
                order_event_buffer_.push(std::move(e));
            };

            for (const auto& f : fill_events) {
                OrderEventDto e;
                e.request_id = static_cast<uint64_t>(f.order_id);
                e.order_id = f.order_id;
                e.symbol = f.symbol;
                e.instrument_type = static_cast<int32_t>(f.instrument_type);
                e.event_type = static_cast<int32_t>(OrderEventType::Filled);
                e.side = static_cast<int32_t>(f.side);
                e.position_side = static_cast<int32_t>(f.position_side);
                e.reduce_only = f.reduce_only;
                e.qty = f.order_qty;
                e.price = f.order_price;
                e.exec_qty = f.exec_qty;
                e.exec_price = f.exec_price;
                e.remaining_qty = f.remaining_qty;
                e.closing_position_id = f.closing_position_id;
                e.is_taker = f.is_taker;
                e.fee = f.fee;
                e.fee_rate = f.fee_rate;
                e.reject_reason = 0;
                order_event_buffer_.push(std::move(e));
                filled_order_ids.insert(f.order_id);
            }

            for (const auto& kv : cur_ord_by_id) {
                if (prev_ord_by_id.find(kv.first) == prev_ord_by_id.end()) {
                    const auto& o = *kv.second;
                    push_order_event(o, OrderEventType::Accepted, 0.0, 0.0, o.quantity);
                }
            }

            for (const auto& kv : prev_ord_by_id) {
                if (cur_ord_by_id.find(kv.first) != cur_ord_by_id.end()) {
                    continue;
                }
                const auto& o = *kv.second;
                if (filled_order_ids.find(o.id) != filled_order_ids.end()) {
                    continue;
                }
                push_order_event(o, OrderEventType::Canceled, 0.0, 0.0, o.quantity);
            }

            LogBatchPooled(logger.get(), order_event_module_id_, order_event_buffer_.events);
        }
    }

    last_event_pos_snapshot_ = cur_positions;
    last_event_ord_snapshot_ = cur_orders;
    last_event_version_ = cur_ver;
}

void BinanceExchange::collect_funding_events(uint64_t ts, std::vector<FundingEventDto>& out)
{
    std::scoped_lock<std::mutex, std::mutex> lk(account_mtx_, state_mtx_);
    collect_funding_events_unlocked_(ts, out);
}

void BinanceExchange::collect_funding_events_unlocked_(uint64_t ts, std::vector<FundingEventDto>& out)
{
    out.clear();
    if (funding_md_.empty()) {
        return;
    }

    out.reserve(symbols_.size());

    for (size_t i = 0; i < funding_md_.size(); ++i) {
        if (i >= symbols_.size()) break;
        if (!has_funding_[i] || !funding_md_[i]) {
            continue;
        }

        auto& data = *funding_md_[i];
        size_t& cur = funding_cursor_[i];

        const size_t end = data.upper_bound_ts(ts);
        while (cur < end) {
            const auto& fr = data.get_funding(cur);
            if (i < has_last_funding_.size()) {
                has_last_funding_[i] = 1;
                last_funding_rate_by_symbol_[i] = fr.Rate;
                last_funding_time_by_symbol_[i] = fr.FundingTime;
            }

            double price = 0.0;
            bool has_price = false;
            if (fr.MarkPrice.has_value()) {
                price = fr.MarkPrice.value();
                has_price = true;
            }
            else if (interpolate_mark_price_(i, fr.FundingTime, price)) {
                has_price = true;
            }
            else if (i < last_close_by_symbol_.size() && has_last_close_[i]) {
                bool age_ok = true;
                if (funding_mark_price_max_age_ms_ > 0 && i < last_close_ts_by_symbol_.size()) {
                    const uint64_t last_ts = last_close_ts_by_symbol_[i];
                    if (fr.FundingTime < last_ts) {
                        age_ok = false;
                    }
                    else {
                        age_ok = (fr.FundingTime - last_ts) <= funding_mark_price_max_age_ms_;
                    }
                }
                if (age_ok) {
                    price = last_close_by_symbol_[i];
                    has_price = true;
                }
            }

            std::vector<Account::FundingApplyResult> applied;
            if (has_price && account_engine_) {
                applied = account_engine_->apply_funding(symbols_[i], fr.FundingTime, fr.Rate, price);
            }

            for (const auto& r : applied) {
                FundingEventDto e;
                e.symbol = symbols_[i];
                if (account_engine_) {
                    e.instrument_type = static_cast<int32_t>(account_engine_->get_instrument_spec(symbols_[i]).type);
                }
                e.funding_time = fr.FundingTime;
                e.rate = fr.Rate;
                e.has_mark_price = has_price;
                e.mark_price = has_price ? price : 0.0;
                e.position_id = static_cast<int64_t>(r.position_id);
                e.is_long = r.is_long;
                e.quantity = r.quantity;
                e.funding = r.funding;
                out.emplace_back(std::move(e));
            }

            ++cur;
        }
    }
}

bool BinanceExchange::interpolate_mark_price_(size_t sym_id, uint64_t ts, double& out_price) const
{
    if (sym_id >= md_.size()) {
        return false;
    }
    const auto& data = md_[sym_id];
    const size_t n = data.get_klines_count();
    if (n == 0) {
        return false;
    }

    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (data.get_kline(mid).Timestamp < ts) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }

    if (lo < n && data.get_kline(lo).Timestamp == ts) {
        out_price = data.get_kline(lo).ClosePrice;
        return std::isfinite(out_price) && out_price > 0.0;
    }

    const bool has_prev = lo > 0;
    const bool has_next = lo < n;
    if (has_prev && has_next) {
        const auto& prev = data.get_kline(lo - 1);
        const auto& next = data.get_kline(lo);
        if (next.Timestamp > prev.Timestamp) {
            const double alpha = static_cast<double>(ts - prev.Timestamp)
                / static_cast<double>(next.Timestamp - prev.Timestamp);
            out_price = prev.ClosePrice + (next.ClosePrice - prev.ClosePrice) * alpha;
        }
        else {
            out_price = next.ClosePrice;
        }
        return std::isfinite(out_price) && out_price > 0.0;
    }

    if (has_prev) {
        const auto& prev = data.get_kline(lo - 1);
        if (funding_mark_price_max_age_ms_ > 0) {
            if (ts < prev.Timestamp || (ts - prev.Timestamp) > funding_mark_price_max_age_ms_) {
                return false;
            }
        }
        out_price = prev.ClosePrice;
        return std::isfinite(out_price) && out_price > 0.0;
    }
    if (has_next) {
        const auto& next = data.get_kline(lo);
        if (funding_mark_price_max_age_ms_ > 0) {
            if (next.Timestamp < ts || (next.Timestamp - ts) > funding_mark_price_max_age_ms_) {
                return false;
            }
        }
        out_price = next.ClosePrice;
        return std::isfinite(out_price) && out_price > 0.0;
    }
    return false;
}

void BinanceExchange::FillStatusSnapshot(StatusSnapshot& out) const
{
    double uncertainty_bps = 0.0;
    {
        std::lock_guard<std::mutex> lk(account_mtx_);
        uncertainty_bps = uncertainty_band_bps_;
        if (account_engine_) {
            const auto perp_bal = account_engine_->get_perp_balance();
            const auto spot_bal = account_engine_->get_spot_balance();
            const auto positions = account_engine_->get_all_positions();
            const double spot_inventory_value = spot_inventory_value_from_positions(positions);
            const double spot_ledger_value = spot_bal.WalletBalance + spot_inventory_value;
            const double total_cash_balance = account_engine_->get_total_cash_balance();
            const double total_ledger_value = perp_bal.Equity + spot_ledger_value;

            out.wallet_balance = perp_bal.WalletBalance;
            out.margin_balance = perp_bal.MarginBalance;
            out.available_balance = perp_bal.AvailableBalance;
            out.unrealized_pnl = perp_bal.UnrealizedPnl;
            out.total_unrealized_pnl = account_engine_->total_unrealized_pnl();
            out.perp_wallet_balance = perp_bal.WalletBalance;
            out.perp_margin_balance = perp_bal.MarginBalance;
            out.perp_available_balance = perp_bal.AvailableBalance;
            out.spot_cash_balance = spot_bal.WalletBalance;
            out.spot_available_balance = spot_bal.AvailableBalance;
            out.spot_inventory_value = spot_inventory_value;
            out.spot_ledger_value = spot_ledger_value;
            out.total_cash_balance = total_cash_balance;
            out.total_ledger_value = total_ledger_value;
            out.total_ledger_value_base = total_ledger_value;
            const double band = std::max(0.0, uncertainty_bps) / 10000.0;
            out.total_ledger_value_conservative = total_ledger_value * (1.0 - band);
            out.total_ledger_value_optimistic = total_ledger_value * (1.0 + band);
        }
        else {
            out.wallet_balance = 0.0;
            out.margin_balance = 0.0;
            out.available_balance = 0.0;
            out.unrealized_pnl = 0.0;
            out.total_unrealized_pnl = 0.0;
            out.perp_wallet_balance = 0.0;
            out.perp_margin_balance = 0.0;
            out.perp_available_balance = 0.0;
            out.spot_cash_balance = 0.0;
            out.spot_available_balance = 0.0;
            out.spot_inventory_value = 0.0;
            out.spot_ledger_value = 0.0;
            out.total_cash_balance = 0.0;
            out.total_ledger_value = 0.0;
            out.total_ledger_value_base = 0.0;
            out.total_ledger_value_conservative = 0.0;
            out.total_ledger_value_optimistic = 0.0;
        }
    }

    {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        out.ts_exchange = last_step_ts_;
        out.progress_pct = progress_pct_unlocked_();
        out.prices.clear();
        out.prices.reserve(symbols_.size());
        for (size_t i = 0; i < symbols_.size(); ++i) {
            StatusSnapshot::PriceSnapshot snap;
            snap.symbol = symbols_[i];
            snap.price = last_close_by_symbol_[i];
            snap.has_price = has_last_close_[i] != 0;
            out.prices.emplace_back(std::move(snap));
        }
    }
    out.uncertainty_band_bps = uncertainty_bps;
}

double BinanceExchange::progress_pct_() const
{
    std::lock_guard<std::mutex> state_lk(state_mtx_);
    return progress_pct_unlocked_();
}

double BinanceExchange::progress_pct_unlocked_() const
{
    if (kline_counts_.empty()) {
        return 0.0;
    }
    double min_ratio = 1.0;
    bool has_count = false;
    const size_t count = std::min(kline_counts_.size(), cursor_.size());
    for (size_t i = 0; i < count; ++i) {
        const auto total = kline_counts_[i];
        if (total == 0) {
            continue;
        }
        has_count = true;
        double ratio = static_cast<double>(cursor_[i]) / static_cast<double>(total);
        if (ratio < min_ratio) {
            min_ratio = ratio;
        }
    }
    if (!has_count) {
        return 0.0;
    }
    if (min_ratio < 0.0) {
        min_ratio = 0.0;
    }
    if (min_ratio > 1.0) {
        min_ratio = 1.0;
    }
    return min_ratio * 100.0;
}

namespace {

static bool position_equal(const dto::Position& x, const dto::Position& y)
{
    return x.id == y.id &&
        x.order_id == y.order_id &&
        x.symbol == y.symbol &&
        x.quantity == y.quantity &&
        x.entry_price == y.entry_price &&
        x.is_long == y.is_long &&
        x.unrealized_pnl == y.unrealized_pnl &&
        x.notional == y.notional &&
        x.initial_margin == y.initial_margin &&
        x.maintenance_margin == y.maintenance_margin &&
        x.fee == y.fee &&
        x.leverage == y.leverage &&
        x.fee_rate == y.fee_rate &&
        x.instrument_type == y.instrument_type;
}

static bool order_equal(const dto::Order& x, const dto::Order& y)
{
    return x.id == y.id &&
        x.symbol == y.symbol &&
        x.quantity == y.quantity &&
        x.price == y.price &&
        x.side == y.side &&
        x.position_side == y.position_side &&
        x.reduce_only == y.reduce_only &&
        x.closing_position_id == y.closing_position_id &&
        x.instrument_type == y.instrument_type &&
        x.client_order_id == y.client_order_id &&
        x.stp_mode == y.stp_mode;
}

template<typename T, typename Eq>
static bool vec_equal_impl(const std::vector<T>& a, const std::vector<T>& b, Eq eq)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), eq);
}

} // namespace

/// @brief Compare two Position snapshots for exact equality.
/// @param a First positions vector.
/// @param b Second positions vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Position>& a,
    const std::vector<dto::Position>& b) {
    return vec_equal_impl(a, b, position_equal);
}

/// @brief Compare two Order snapshots for exact equality.
/// @param a First orders vector.
/// @param b Second orders vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Order>& a,
    const std::vector<dto::Order>& b) {
    return vec_equal_impl(a, b, order_equal);
}

