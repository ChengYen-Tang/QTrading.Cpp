#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"
#include "Diagnostics/Trace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
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


std::vector<BinanceExchange::SymbolDataset> to_datasets(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv)
{
    std::vector<BinanceExchange::SymbolDataset> out;
    out.reserve(symbolCsv.size());
    for (const auto& [sym, csv] : symbolCsv) {
        out.push_back(BinanceExchange::SymbolDataset{ sym, csv, std::nullopt });
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
    const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level, uint64_t run_id)
    : BinanceExchange(datasets, logger, std::make_shared<Account>(init_balance, vip_level), run_id) { }

/// @brief Primary constructor with an external Account instance.
/// @param symbolCsv  Vector of (symbol, csv_file) pairs to drive market data.
/// @param logger     Shared Logger for writing account/position/order logs.
/// @param account    Shared Account simulation object.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account,
    uint64_t run_id)
    : BinanceExchange(to_datasets(symbolCsv), logger, account, run_id)
{
}

BinanceExchange::BinanceExchange(
    const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account,
    uint64_t run_id)
    : logger(logger), account(account)
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
    has_last_close_.assign(symbols_.size(), 0);

    funding_md_.resize(datasets.size());
    funding_cursor_.assign(datasets.size(), 0);
    has_funding_.assign(datasets.size(), 0);
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

        log_status_snapshot(task.balance, task.positions, task.orders, task.cur_ver);
        if (enable_event_logging_) {
            log_events(task.ctx, *task.market, task.positions, task.orders, task.funding_events,
                task.balance, std::move(task.fill_events), task.cur_ver);
        }
    }
}

bool BinanceExchange::place_order(const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->place_order(symbol, quantity, price, side, position_side, reduce_only);
}

bool BinanceExchange::place_order(const std::string& symbol,
    double quantity,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->place_order(symbol, quantity, side, position_side, reduce_only);
}

void BinanceExchange::close_position(const std::string& symbol, double price)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol, price);
}

void BinanceExchange::close_position(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol);
}

void BinanceExchange::close_position(const std::string& symbol,
    QTrading::Dto::Trading::PositionSide position_side,
    double price)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol, position_side, price);
}

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    QTR_TRACE("ex", "step begin");

    uint64_t ts;
    /// @details Find the next minimum timestamp among all symbols.
    if (!next_timestamp(ts)) {
        QTR_TRACE("ex", "no next timestamp -> close channels");
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    QTrading::Dto::Account::BalanceSnapshot balance{};
    std::vector<Account::FillEvent> fill_events;
    std::vector<dto::Position> curP;
    std::vector<dto::Order> curO;
    uint64_t cur_ver = 0;
    MultiKlinePtr dto;

    {
        std::lock_guard<std::mutex> lk(account_mtx_);

        // Terminate simulation (end backtest) once wallet balance is depleted.
        if (account->get_balance().WalletBalance <= 0.0) {
            QTR_TRACE("ex", "balance depleted -> close channels");
            market_channel->Close();
            position_channel->Close();
            order_channel->Close();
            return false;
        }

        /// @details Publish multi-symbol market data at timestamp `ts`.
        last_step_ts_ = ts;
        log_ctx_.ts_exchange = ts;
        log_ctx_.step_seq += 1;
        log_ctx_.event_seq = 0;
        set_global_timestamp(ts);
        dto = make_pooled<MultiKlineDto>();
        build_multikline(ts, *dto);

        collect_funding_events(ts, funding_events_scratch_);

        cur_ver = account->get_state_version();
        curP = account->get_all_positions();
        curO = account->get_all_open_orders();
        balance = account->get_balance();
        fill_events = account->drain_fill_events();
    }

    log_ctx_.ts_local = now_ms();

    QTR_TRACE("ex", "market_channel Send begin");
    market_channel->Send(dto);
    QTR_TRACE("ex", "market_channel Send end");

    bool pos_changed = false;
    bool ord_changed = false;

    // Only consider sending snapshots when account reported a state transition.
    if (cur_ver != last_account_version_) {
        if (!vec_equal(curP, last_pos_snapshot)) {
            QTR_TRACE("ex", "position_channel Send");
            position_channel->Send(curP);
            pos_changed = true;
        }

        if (!vec_equal(curO, last_ord_snapshot)) {
            QTR_TRACE("ex", "order_channel Send");
            order_channel->Send(curO);
            ord_changed = true;
        }
    }

    if (logger) {
        LogTask task;
        task.ctx = log_ctx_;
        task.market = dto;
        task.positions = curP;
        task.orders = curO;
        task.funding_events = funding_events_scratch_;
        task.balance = balance;
        task.fill_events = std::move(fill_events);
        task.cur_ver = cur_ver;
        enqueue_log_task_(std::move(task));
    }

    if (cur_ver != last_account_version_) {
        if (pos_changed) {
            last_pos_snapshot = curP;
        }
        if (ord_changed) {
            last_ord_snapshot = curO;
        }
        last_account_version_ = cur_ver;
    }

    QTR_TRACE("ex", "step end");
    return true;
}

/// @brief Get a snapshot of all current positions.
/// @return Const reference to the vector of Position DTOs.
const std::vector<dto::Position>& BinanceExchange::get_all_positions() const {
    return account->get_all_positions();
}

/// @brief Get a snapshot of all current open orders.
/// @return Const reference to the vector of Order DTOs.
const std::vector<dto::Order>& BinanceExchange::get_all_open_orders() const {
    return account->get_all_open_orders();
}

void BinanceExchange::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->set_symbol_leverage(symbol, new_leverage);
}

double BinanceExchange::get_symbol_leverage(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->get_symbol_leverage(symbol);
}

/// @brief Close the simulator: drain CSVs and close all channels.
/// @details Advances each symbol's cursor to the end before closing.
void BinanceExchange::close()
{
    for (size_t i = 0; i < md_.size(); ++i) {
        cursor_[i] = md_[i].get_klines_count();
        has_next_ts_[i] = 0;
    }

	IExchange<MultiKlinePtr>::close();
}

void BinanceExchange::cancel_open_orders(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->cancel_open_orders(symbol);
}

/// @brief Determine the next global timestamp to emit.
/// @param[out] ts  The minimum upcoming timestamp among all symbols.
/// @return true if at least one symbol has data remaining.
bool BinanceExchange::next_timestamp(uint64_t& ts)
{
    // Pop stale heap entries until top matches current per-symbol next_ts.
    while (!next_ts_heap_.empty()) {
        const auto top = next_ts_heap_.top();
        if (top.sym_id < next_ts_by_symbol_.size() &&
            has_next_ts_[top.sym_id] &&
            next_ts_by_symbol_[top.sym_id] == top.ts) {
            ts = top.ts;
            return true;
        }
        next_ts_heap_.pop();
    }
    ts = std::numeric_limits<uint64_t>::max();
    return false;
}

/// @brief Build and send a MultiKlineDto for timestamp `ts`.
/// @param ts   Global timestamp to align on.
/// @param out  DTO to populate with per-symbol optional KlineDto.
void BinanceExchange::build_multikline(uint64_t ts, MultiKlineDto& out)
{
    out.Timestamp = ts;
    out.symbols = symbols_shared_;
    out.klines.clear();
    if (enable_klines_map_) {
        out.klines.reserve(md_.size());
    }
    out.klines_by_id.clear();
    out.klines_by_id.resize(symbols_.size());

    kline_snap_cache_.clear();
    kline_snap_cache_.reserve(md_.size());

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
            kline_snap_cache_.emplace(sym, k);
            last_close_by_symbol_[i] = k.ClosePrice;
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
    }
    if (!kline_snap_cache_.empty())
    {
        account->update_positions(kline_snap_cache_);
    }
}

/// @brief Log account balance, positions, and orders via the Logger.
/// @details Uses Arrow Feather-V2 for efficient batch logging.
void BinanceExchange::log_status_snapshot(const QTrading::Dto::Account::BalanceSnapshot& balance,
    const std::vector<dto::Position>& positions,
    const std::vector<dto::Order>& orders,
    uint64_t cur_ver)
{
    if (!logger) {
        return;
    }

    if (cur_ver == last_logged_version_) {
        return;
    }

    if (account_module_id_ != Logger::kInvalidModuleId) {
        logger->Log(account_module_id_, make_log_payload<AccountLog>(
            AccountLog{ balance.WalletBalance, balance.UnrealizedPnl, balance.MarginBalance }));
    }
    if (position_module_id_ != Logger::kInvalidModuleId) {
        LogBatchPooled(logger.get(), position_module_id_, positions);
    }
    if (order_module_id_ != Logger::kInvalidModuleId) {
        LogBatchPooled(logger.get(), order_module_id_, orders);
    }

    last_logged_version_ = cur_ver;
}

void BinanceExchange::log_events(QTrading::Infra::Logging::StepLogContext ctx,
    const MultiKlineDto& market,
    const std::vector<dto::Position>& cur_positions,
    const std::vector<dto::Order>& cur_orders,
    const std::vector<FundingEventDto>& funding_events,
    const QTrading::Dto::Account::BalanceSnapshot& balance,
    std::vector<Account::FillEvent>&& fill_events,
    uint64_t cur_ver)
{
    if (!logger) {
        return;
    }
    if (!enable_event_logging_) {
        return;
    }

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
            fill_events.empty() ? balance.WalletBalance : fill_events.front().balance_snapshot.WalletBalance);

        for (const auto& f : fill_events) {
            AccountEventDto e;
            e.request_id = static_cast<uint64_t>(f.order_id);
            e.source_order_id = f.order_id;
            e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
            e.wallet_delta = f.balance_snapshot.WalletBalance - last_wallet_for_fill;
            e.wallet_balance_after = f.balance_snapshot.WalletBalance;
            e.margin_balance_after = f.balance_snapshot.MarginBalance;
            e.available_balance_after = f.balance_snapshot.AvailableBalance;
            account_event_buffer_.push(std::move(e));
            last_wallet_for_fill = f.balance_snapshot.WalletBalance;
        }

        AccountEventDto e;
        e.request_id = 0;
        e.source_order_id = -1;
        e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
        e.wallet_delta = balance.WalletBalance - last_wallet_for_fill;
        e.wallet_balance_after = balance.WalletBalance;
        e.margin_balance_after = balance.MarginBalance;
        e.available_balance_after = balance.AvailableBalance;
        account_event_buffer_.push(std::move(e));

        LogBatchPooled(logger.get(), account_event_module_id_, account_event_buffer_.events);
    }
    last_wallet_balance_ = balance.WalletBalance;

    const bool need_position_events = position_event_module_id_ != Logger::kInvalidModuleId;
    const bool need_order_events = order_event_module_id_ != Logger::kInvalidModuleId;
    if (need_position_events || need_order_events) {
        std::unordered_map<int, const dto::Position*> prev_pos_by_id;
        prev_pos_by_id.reserve(last_event_pos_snapshot_.size());
        for (const auto& p : last_event_pos_snapshot_) {
            prev_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_map<int, const dto::Position*> cur_pos_by_id;
        cur_pos_by_id.reserve(cur_positions.size());
        for (const auto& p : cur_positions) {
            cur_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_set<int> changed_position_ids;
        changed_position_ids.reserve(cur_positions.size() + last_event_pos_snapshot_.size());

        auto push_position_event = [&](const dto::Position& p, PositionEventType type) {
            PositionEventDto e;
            e.request_id = static_cast<uint64_t>(p.order_id);
            e.source_order_id = p.order_id;
            e.position_id = p.id;
            e.symbol = p.symbol;
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
            prev_ord_by_id.reserve(last_event_ord_snapshot_.size());
            for (const auto& o : last_event_ord_snapshot_) {
                prev_ord_by_id.emplace(o.id, &o);
            }

            std::unordered_map<int, const dto::Order*> cur_ord_by_id;
            cur_ord_by_id.reserve(cur_orders.size());
            for (const auto& o : cur_orders) {
                cur_ord_by_id.emplace(o.id, &o);
            }

            auto push_order_event = [&](const dto::Order& o, OrderEventType type,
                double exec_qty, double exec_price, double remaining_qty) {
                OrderEventDto e;
                e.request_id = static_cast<uint64_t>(o.id);
                e.order_id = o.id;
                e.symbol = o.symbol;
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

            double price = 0.0;
            bool has_price = false;
            if (fr.MarkPrice.has_value()) {
                price = fr.MarkPrice.value();
                has_price = true;
            }
            else if (i < last_close_by_symbol_.size() && has_last_close_[i]) {
                price = last_close_by_symbol_[i];
                has_price = true;
            }

            std::vector<Account::FundingApplyResult> applied;
            if (has_price && account) {
                applied = account->apply_funding(symbols_[i], fr.FundingTime, fr.Rate, price);
            }

            for (const auto& r : applied) {
                FundingEventDto e;
                e.symbol = symbols_[i];
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

void BinanceExchange::FillStatusSnapshot(StatusSnapshot& out) const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    out.ts_exchange = last_step_ts_;
    if (account) {
        const auto bal = account->get_balance();
        out.wallet_balance = bal.WalletBalance;
        out.margin_balance = bal.MarginBalance;
        out.available_balance = bal.AvailableBalance;
        out.unrealized_pnl = bal.UnrealizedPnl;
        out.total_unrealized_pnl = account->total_unrealized_pnl();
    }
    else {
        out.wallet_balance = 0.0;
        out.margin_balance = 0.0;
        out.available_balance = 0.0;
        out.unrealized_pnl = 0.0;
        out.total_unrealized_pnl = 0.0;
    }
    out.progress_pct = progress_pct_();
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

double BinanceExchange::progress_pct_() const
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
        x.fee_rate == y.fee_rate;
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
        x.closing_position_id == y.closing_position_id;
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
