#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"
#include "Diagnostics/Trace.hpp"
#include "Time/ReplayTimeRange.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <memory_resource>
#include <stdexcept>
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
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Perp
        });
    }
    return out;
}

size_t lower_bound_kline_ts(const MarketData& data, uint64_t ts)
{
    size_t lo = 0;
    size_t hi = data.get_klines_count();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (data.get_kline(mid).Timestamp < ts) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

size_t upper_bound_kline_ts(const MarketData& data, uint64_t ts)
{
    size_t lo = 0;
    size_t hi = data.get_klines_count();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (data.get_kline(mid).Timestamp <= ts) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

size_t lower_bound_funding_ts(const FundingRateData& data, uint64_t ts)
{
    size_t lo = 0;
    size_t hi = data.get_count();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (data.get_funding(mid).FundingTime < ts) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

} // namespace

class BinanceExchange::IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(LogTask&& task) = 0;
};

class BinanceExchange::NullEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    void publish(LogTask&&) override {}
};

class BinanceExchange::LegacyLogAdapter final {
public:
    explicit LegacyLogAdapter(BinanceExchange& owner)
        : owner_(owner) {}

    void publish(LogTask&& task)
    {
        owner_.log_status_snapshot(
            task.perp_balance,
            task.spot_balance,
            task.total_cash_balance,
            task.spot_inventory_value,
            task.positions,
            task.orders,
            task.cur_ver);
        owner_.log_events(task.ctx,
            task.market_events,
            task.positions,
            task.orders,
            task.funding_events,
            task.perp_balance,
            task.spot_balance,
            task.total_cash_balance,
            task.spot_inventory_value,
            std::move(task.fill_events),
            task.cur_ver);
    }

private:
    BinanceExchange& owner_;
};

class BinanceExchange::LegacyEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    explicit LegacyEventPublisher(BinanceExchange& owner)
        : adapter_(owner) {}

    void publish(LogTask&& task) override
    {
        adapter_.publish(std::move(task));
    }

private:
    LegacyLogAdapter adapter_;
};

class BinanceExchange::AsyncEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    explicit AsyncEventPublisher(std::unique_ptr<BinanceExchange::IEventPublisher> downstream)
        : downstream_(std::move(downstream))
    {
        worker_thread_ = std::thread([this]() { worker_(); });
    }

    ~AsyncEventPublisher() override
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void publish(LogTask&& task) override
    {
        if (!downstream_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.emplace_back(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_()
    {
        while (true) {
            LogTask task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]() {
                    return stop_.load(std::memory_order_acquire) || !queue_.empty();
                    });
                if (stop_.load(std::memory_order_acquire) && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            downstream_->publish(std::move(task));
        }
    }

    std::unique_ptr<BinanceExchange::IEventPublisher> downstream_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<LogTask> queue_;
    std::thread worker_thread_;
    std::atomic<bool> stop_{ false };
};

class BinanceExchange::MarketReplayEngine final {
public:
    static bool NextTimestamp(BinanceExchange& owner, uint64_t& ts);
    static void BuildMultiKline(BinanceExchange& owner, uint64_t ts, MultiKlineDto& out);
};

class BinanceExchange::SimulatorRiskOverlayEngine final {
public:
    static void BeginStep(BinanceExchange& owner);
    static void ConsumeSymbolPrices(BinanceExchange& owner,
        size_t sym_id,
        bool has_mark,
        double mark,
        bool has_index,
        double index,
        double& max_abs_basis_bps,
        uint32_t& warning_symbols,
        uint32_t& stress_symbols);
    static void FinalizeStep(BinanceExchange& owner,
        double max_abs_basis_bps,
        uint32_t warning_symbols,
        uint32_t stress_symbols);
    static void CollectLeverageCaps(const BinanceExchange& owner,
        std::vector<std::pair<std::string, double>>& out);
    static bool IsOpeningBlockedByStress(const BinanceExchange& owner,
        Account& acc,
        const std::string& symbol,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only);
};

class BinanceExchange::StatusSnapshotBuilder final {
public:
    static void Fill(const BinanceExchange& owner, BinanceExchange::StatusSnapshot& out);
};

class BinanceExchange::CoreDispatchFacade final {
public:
    static RunStepResult RunStep(BinanceExchange& owner, CoreMode mode)
    {
        return owner.dispatch_step_(mode);
    }
};

class BinanceExchange::LegacyCoreSessionAdapter final {
public:
    static RunStepResult Run(BinanceExchange& owner);
};

class BinanceExchange::NewCoreSessionAdapter final {
public:
    static RunStepResult Run(BinanceExchange&)
    {
        RunStepResult result{};
        // Milestone 1 skeleton: new core path is wired but intentionally disabled.
        result.progressed = false;
        result.fallback_to_legacy = true;
        result.compare_snapshot_ready = false;
        return result;
    }
};

class BinanceExchange::ExchangeSession final {
public:
    explicit ExchangeSession(BinanceExchange& owner)
        : owner_(owner) {}

    bool Run();

private:
    BinanceExchange& owner_;
};

BinanceExchange::RunStepResult BinanceExchange::LegacyCoreSessionAdapter::Run(BinanceExchange& owner)
{
    RunStepResult result{};
    result.progressed = ExchangeSession(owner).Run();
    result.compare_snapshot_ready = true;
    return result;
}

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
    if (run_id == 0) {
        run_id = now_ms();
    }
    log_ctx_.run_id = run_id;
    log_ctx_.step_seq = 0;
    log_ctx_.ts_exchange = 0;
    log_ctx_.event_seq = 0;

    if (account_engine_) {
        account_engine_->set_strict_symbol_registration_mode(account_engine_->is_strict_binance_mode());
    }

    // Load each CSV in parallel (safe: each MarketData instance is independent).
    std::vector<std::future<MarketData>> jobs;
    symbols_.reserve(datasets.size());
    md_.reserve(datasets.size());
    mark_md_.resize(datasets.size());
    index_md_.resize(datasets.size());
    has_mark_md_.assign(datasets.size(), 0);
    has_index_md_.assign(datasets.size(), 0);
    cursor_.assign(datasets.size(), 0);
    kline_window_begin_idx_.assign(datasets.size(), 0);
    kline_window_end_idx_.assign(datasets.size(), 0);
    next_ts_by_symbol_.assign(datasets.size(), 0);
    has_next_ts_.assign(datasets.size(), 0);
    kline_counts_.reserve(datasets.size());
    jobs.reserve(datasets.size());

    for (const auto& ds : datasets) {
        symbols_.push_back(ds.symbol);
        if (account_engine_) {
            const auto type = ds.instrument_type.value_or(QTrading::Dto::Trading::InstrumentType::Perp);
            account_engine_->set_instrument_type(ds.symbol, type);
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
        const size_t total = data.get_klines_count();
        kline_counts_.push_back(total);
        kline_window_begin_idx_[i] = 0;
        kline_window_end_idx_[i] = total;
    }

    // Load optional per-symbol mark/index datasets.
    for (size_t i = 0; i < datasets.size(); ++i) {
        const auto& ds = datasets[i];
        try {
            if (ds.mark_kline_csv.has_value() && !ds.mark_kline_csv->empty()) {
                mark_md_[i] = std::make_unique<MarketData>(ds.symbol, *ds.mark_kline_csv);
                has_mark_md_[i] = 1;
            }
            if (ds.index_kline_csv.has_value() && !ds.index_kline_csv->empty()) {
                index_md_[i] = std::make_unique<MarketData>(ds.symbol, *ds.index_kline_csv);
                has_index_md_[i] = 1;
            }
        }
        catch (const std::exception& ex) {
            throw std::runtime_error(
                "Failed to load mark/index dataset for symbol '" + ds.symbol + "': " + ex.what());
        }
    }

    const auto replay_range = QTrading::Utils::Time::ParseReplayTimeRangeFromEnv();
    if (!replay_range.ok()) {
        throw std::runtime_error(replay_range.error);
    }
    replay_start_ts_ms_ = replay_range.start_ms;
    replay_end_ts_ms_ = replay_range.end_ms;

    for (size_t i = 0; i < md_.size(); ++i) {
        const auto& data = md_[i];
        size_t begin_idx = 0;
        size_t end_idx = data.get_klines_count();
        if (replay_start_ts_ms_.has_value()) {
            begin_idx = lower_bound_kline_ts(data, *replay_start_ts_ms_);
        }
        if (replay_end_ts_ms_.has_value()) {
            end_idx = upper_bound_kline_ts(data, *replay_end_ts_ms_);
        }
        if (begin_idx > end_idx) {
            begin_idx = end_idx;
        }
        kline_window_begin_idx_[i] = begin_idx;
        kline_window_end_idx_[i] = end_idx;
        cursor_[i] = begin_idx;

        if (cursor_[i] < kline_window_end_idx_[i]) {
            const uint64_t ts = data.get_kline(cursor_[i]).Timestamp;
            next_ts_by_symbol_[i] = ts;
            has_next_ts_[i] = 1;
            next_ts_heap_.push(HeapItem{ ts, i });
        }
    }

    last_close_by_symbol_.assign(symbols_.size(), 0.0);
    last_close_ts_by_symbol_.assign(symbols_.size(), 0);
    has_last_close_.assign(symbols_.size(), 0);
    last_mark_by_symbol_.assign(symbols_.size(), 0.0);
    last_mark_ts_by_symbol_.assign(symbols_.size(), 0);
    has_last_mark_.assign(symbols_.size(), 0);
    last_mark_source_by_symbol_.assign(symbols_.size(),
        static_cast<int32_t>(ReferencePriceSource::None));
    last_index_by_symbol_.assign(symbols_.size(), 0.0);
    last_index_ts_by_symbol_.assign(symbols_.size(), 0);
    has_last_index_.assign(symbols_.size(), 0);
    last_index_source_by_symbol_.assign(symbols_.size(),
        static_cast<int32_t>(ReferencePriceSource::None));
    last_mark_index_basis_bps_by_symbol_.assign(symbols_.size(), 0.0);
    has_last_mark_index_basis_.assign(symbols_.size(), 0);
    simulator_risk_overlay_.warning_active_by_symbol.assign(symbols_.size(), 0);
    simulator_risk_overlay_.stress_active_by_symbol.assign(symbols_.size(), 0);
    simulator_risk_overlay_.warning_symbols = 0;
    simulator_risk_overlay_.stress_symbols = 0;
    simulator_risk_overlay_.stress_blocked_orders.store(0, std::memory_order_relaxed);
    funding_applied_events_total_ = 0;
    funding_skipped_no_mark_total_ = 0;

    funding_md_.resize(datasets.size());
    funding_cursor_.assign(datasets.size(), 0);
    funding_window_end_idx_.assign(datasets.size(), 0);
    has_funding_.assign(datasets.size(), 0);
    last_funding_rate_by_symbol_.assign(datasets.size(), 0.0);
    last_funding_time_by_symbol_.assign(datasets.size(), 0);
    has_last_funding_.assign(datasets.size(), 0);
    for (size_t i = 0; i < datasets.size(); ++i) {
        const auto& ds = datasets[i];
        if (ds.funding_csv.has_value() && !ds.funding_csv->empty()) {
            funding_md_[i] = std::make_unique<FundingRateData>(ds.symbol, *ds.funding_csv);
            has_funding_[i] = 1;
            funding_window_end_idx_[i] = funding_md_[i]->get_count();
        }
    }

    for (size_t i = 0; i < funding_md_.size(); ++i) {
        if (!has_funding_[i] || !funding_md_[i]) {
            continue;
        }
        const auto& data = *funding_md_[i];
        size_t begin_idx = 0;
        size_t end_idx = data.get_count();
        if (replay_start_ts_ms_.has_value()) {
            begin_idx = lower_bound_funding_ts(data, *replay_start_ts_ms_);
        }
        if (replay_end_ts_ms_.has_value()) {
            end_idx = data.upper_bound_ts(*replay_end_ts_ms_);
        }
        if (begin_idx > end_idx) {
            begin_idx = end_idx;
        }
        funding_cursor_[i] = begin_idx;
        funding_window_end_idx_[i] = end_idx;
    }

    /// @details Create bounded channels:
    ///          - market_channel: 8-element sliding window of MultiKlineDto
    ///          - position_channel / order_channel: debounce buffers (latest snapshot is sufficient)
    market_channel = ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(8, OverflowPolicy::DropOldest);
    position_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Position>>();
    order_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Order>>();

    if (logger) {
        event_publisher_ = std::make_unique<AsyncEventPublisher>(
            std::make_unique<LegacyEventPublisher>(*this));
    }
    else {
        event_publisher_ = std::make_unique<NullEventPublisher>();
    }
}

BinanceExchange::~BinanceExchange()
{
}

void BinanceExchange::publish_log_task_(LogTask&& task)
{
    if (!event_publisher_) {
        return;
    }
    event_publisher_->publish(std::move(task));
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

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    QTR_TRACE("ex", "step begin");
    return run_step_session_();
}

void BinanceExchange::set_core_mode(CoreMode mode)
{
    CoreMode effective_mode = CoreMode::LegacyOnly;
    switch (mode) {
    case CoreMode::LegacyOnly:
    case CoreMode::NewCoreShadow:
    case CoreMode::NewCorePrimary:
        effective_mode = mode;
        break;
    default:
        effective_mode = CoreMode::LegacyOnly;
        break;
    }

    core_mode_.store(effective_mode, std::memory_order_release);
}

BinanceExchange::CoreMode BinanceExchange::core_mode() const
{
    return core_mode_.load(std::memory_order_acquire);
}

std::optional<BinanceExchange::StepCompareDiagnostic> BinanceExchange::consume_last_compare_diagnostic()
{
    std::lock_guard<std::mutex> lk(compare_diag_mtx_);
    auto out = std::move(last_compare_diagnostic_);
    last_compare_diagnostic_.reset();
    return out;
}

bool BinanceExchange::run_step_session_()
{
    const CoreMode mode = core_mode_.load(std::memory_order_acquire);
    return CoreDispatchFacade::RunStep(*this, mode).progressed;
}

BinanceExchange::RunStepResult BinanceExchange::dispatch_step_(CoreMode mode)
{
    switch (mode) {
    case CoreMode::LegacyOnly:
        return LegacyCoreSessionAdapter::Run(*this);

    case CoreMode::NewCoreShadow:
    {
        auto legacy_result = LegacyCoreSessionAdapter::Run(*this);
        StepCompareDiagnostic diagnostic{};
        diagnostic.mode = mode;
        diagnostic.legacy = build_step_compare_snapshot_(legacy_result);

        const auto new_core_result = NewCoreSessionAdapter::Run(*this);
        if (new_core_result.compare_snapshot_ready) {
            diagnostic.compared = true;
            diagnostic.candidate = build_step_compare_snapshot_(new_core_result);
            const auto mismatch_reason =
                compare_step_snapshots_(diagnostic.legacy, diagnostic.candidate);
            diagnostic.matched = !mismatch_reason.has_value();
            if (mismatch_reason.has_value()) {
                diagnostic.reason = *mismatch_reason;
            }
        }
        else {
            diagnostic.compared = false;
            diagnostic.matched = true;
            diagnostic.reason = "New core compare snapshot unavailable; shadow mode kept legacy result.";
        }
        record_compare_diagnostic_(std::move(diagnostic));
        return legacy_result;
    }

    case CoreMode::NewCorePrimary:
    {
        const auto new_core_result = NewCoreSessionAdapter::Run(*this);
        if (new_core_result.progressed && !new_core_result.fallback_to_legacy) {
            return new_core_result;
        }

        auto legacy_result = LegacyCoreSessionAdapter::Run(*this);
        legacy_result.fallback_to_legacy = true;

        StepCompareDiagnostic diagnostic{};
        diagnostic.mode = mode;
        diagnostic.legacy = build_step_compare_snapshot_(legacy_result);
        if (new_core_result.compare_snapshot_ready) {
            diagnostic.compared = true;
            diagnostic.candidate = build_step_compare_snapshot_(new_core_result);
            const auto mismatch_reason =
                compare_step_snapshots_(diagnostic.legacy, diagnostic.candidate);
            diagnostic.matched = !mismatch_reason.has_value();
            if (mismatch_reason.has_value()) {
                diagnostic.reason = *mismatch_reason;
            }
        }
        else {
            diagnostic.compared = false;
            diagnostic.matched = true;
            diagnostic.reason = "New core primary unavailable; fell back to legacy path.";
        }
        record_compare_diagnostic_(std::move(diagnostic));
        return legacy_result;
    }

    default:
        return LegacyCoreSessionAdapter::Run(*this);
    }
}

BinanceExchange::StepCompareSnapshot BinanceExchange::build_step_compare_snapshot_(
    const RunStepResult& result) const
{
    StepCompareSnapshot snapshot{};
    snapshot.progressed = result.progressed;

    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        snapshot.ts_exchange = last_step_ts_;
        snapshot.step_seq = log_ctx_.step_seq;
    }

    {
        std::lock_guard<std::mutex> lk(account_mtx_);
        if (account_engine_) {
            snapshot.position_count = static_cast<uint64_t>(account_engine_->get_all_positions().size());
            snapshot.open_order_count = static_cast<uint64_t>(account_engine_->get_all_open_orders().size());
            const auto perp_balance = account_engine_->get_perp_balance();
            const auto spot_balance = account_engine_->get_spot_balance();
            snapshot.perp_wallet_balance = perp_balance.WalletBalance;
            snapshot.spot_wallet_balance = spot_balance.WalletBalance;
            snapshot.total_cash_balance = account_engine_->get_total_cash_balance();
        }
    }

    return snapshot;
}

std::optional<std::string> BinanceExchange::compare_step_snapshots_(
    const StepCompareSnapshot& legacy_snapshot,
    const StepCompareSnapshot& candidate_snapshot) const
{
    if (legacy_snapshot.progressed != candidate_snapshot.progressed) {
        return std::string("compare mismatch: progressed differs");
    }
    if (legacy_snapshot.ts_exchange != candidate_snapshot.ts_exchange) {
        return std::string("compare mismatch: ts_exchange differs");
    }
    if (legacy_snapshot.step_seq != candidate_snapshot.step_seq) {
        return std::string("compare mismatch: step_seq differs");
    }
    if (legacy_snapshot.position_count != candidate_snapshot.position_count) {
        return std::string("compare mismatch: position_count differs");
    }
    if (legacy_snapshot.open_order_count != candidate_snapshot.open_order_count) {
        return std::string("compare mismatch: open_order_count differs");
    }

    constexpr double kEpsilon = 1e-9;
    const auto almost_equal = [](double lhs, double rhs) {
        return std::fabs(lhs - rhs) <= kEpsilon;
    };

    if (!almost_equal(legacy_snapshot.perp_wallet_balance, candidate_snapshot.perp_wallet_balance)) {
        return std::string("compare mismatch: perp_wallet_balance differs");
    }
    if (!almost_equal(legacy_snapshot.spot_wallet_balance, candidate_snapshot.spot_wallet_balance)) {
        return std::string("compare mismatch: spot_wallet_balance differs");
    }
    if (!almost_equal(legacy_snapshot.total_cash_balance, candidate_snapshot.total_cash_balance)) {
        return std::string("compare mismatch: total_cash_balance differs");
    }

    return std::nullopt;
}

void BinanceExchange::record_compare_diagnostic_(StepCompareDiagnostic diag)
{
    if (!diag.reason.empty()) {
        QTR_TRACE("ex", diag.reason);
    }
    std::lock_guard<std::mutex> lk(compare_diag_mtx_);
    last_compare_diagnostic_ = std::move(diag);
}

bool BinanceExchange::ExchangeSession::Run()
{
    uint64_t ts;
    {
        std::lock_guard<std::mutex> state_lk(owner_.state_mtx_);
        if (!owner_.next_timestamp(ts)) {
            QTR_TRACE("ex", "no next timestamp -> close channels");
            owner_.market_channel->Close();
            owner_.position_channel->Close();
            owner_.order_channel->Close();
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
    std::vector<LogTask::DomainMarketEvent> market_events;
    bool has_trade_kline_for_update = false;

    {
        std::lock_guard<std::mutex> lk(owner_.account_mtx_);
        const uint64_t next_step = owner_.processed_steps_ + 1;
        owner_.flush_deferred_orders_locked_(next_step);
        owner_.processed_steps_ = next_step;
        owner_.set_global_timestamp(ts);
        apply_timing = owner_.funding_apply_timing_;

        const auto initial_perp_balance = owner_.account_engine_->get_perp_balance();
        const auto initial_spot_balance = owner_.account_engine_->get_spot_balance();
        const bool no_positions = owner_.account_engine_->get_all_positions().empty();
        const bool no_orders = owner_.account_engine_->get_all_open_orders().empty();
        if (initial_perp_balance.WalletBalance <= 0.0 &&
            initial_spot_balance.WalletBalance <= 0.0 &&
            no_positions &&
            no_orders) {
            QTR_TRACE("ex", "balance depleted -> close channels");
            owner_.market_channel->Close();
            owner_.position_channel->Close();
            owner_.order_channel->Close();
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> state_lk(owner_.state_mtx_);
        owner_.last_step_ts_ = ts;
        owner_.log_ctx_.ts_exchange = ts;
        owner_.log_ctx_.step_seq += 1;
        owner_.log_ctx_.event_seq = 0;
        step_log_ctx = owner_.log_ctx_;
    }
    dto = make_pooled<MultiKlineDto>();

    auto build_market = [&]() {
        std::lock_guard<std::mutex> state_lk(owner_.state_mtx_);
        owner_.build_multikline(ts, *dto);

        owner_.market_update_by_id_.market.symbols = owner_.symbols_shared_;
        owner_.market_update_by_id_.market.ts_exchange = ts;
        owner_.market_update_by_id_.market.trade_klines_by_id.clear();
        owner_.market_update_by_id_.market.trade_klines_by_id.resize(owner_.symbols_.size(), nullptr);
        owner_.market_update_by_id_.reference.mark_price_by_id.clear();
        owner_.market_update_by_id_.reference.mark_price_by_id.resize(owner_.symbols_.size(), 0.0);
        owner_.market_update_by_id_.reference.has_mark_price_by_id.clear();
        owner_.market_update_by_id_.reference.has_mark_price_by_id.resize(owner_.symbols_.size(), 0);
        owner_.market_update_by_id_.reference.mark_price_source_by_id.clear();
        owner_.market_update_by_id_.reference.mark_price_source_by_id.resize(
            owner_.symbols_.size(), static_cast<int32_t>(ReferencePriceSource::None));
        owner_.market_update_by_id_.reference.index_price_by_id.clear();
        owner_.market_update_by_id_.reference.index_price_by_id.resize(owner_.symbols_.size(), 0.0);
        owner_.market_update_by_id_.reference.has_index_price_by_id.clear();
        owner_.market_update_by_id_.reference.has_index_price_by_id.resize(owner_.symbols_.size(), 0);
        owner_.market_update_by_id_.reference.index_price_source_by_id.clear();
        owner_.market_update_by_id_.reference.index_price_source_by_id.resize(
            owner_.symbols_.size(), static_cast<int32_t>(ReferencePriceSource::None));
        owner_.market_update_by_id_.funding.funding_rate_by_id.clear();
        owner_.market_update_by_id_.funding.funding_rate_by_id.resize(owner_.symbols_.size(), 0.0);
        owner_.market_update_by_id_.funding.funding_time_by_id.clear();
        owner_.market_update_by_id_.funding.funding_time_by_id.resize(owner_.symbols_.size(), 0);
        owner_.market_update_by_id_.funding.has_funding_by_id.clear();
        owner_.market_update_by_id_.funding.has_funding_by_id.resize(owner_.symbols_.size(), 0);

        has_trade_kline_for_update = false;
        market_events.clear();
        market_events.reserve(owner_.symbols_.size());
        double max_abs_basis_bps = 0.0;
        uint32_t warning_symbols = 0;
        uint32_t stress_symbols = 0;
        SimulatorRiskOverlayEngine::BeginStep(owner_);
        for (size_t i = 0; i < owner_.symbols_.size(); ++i) {
            LogTask::DomainMarketEvent market_event;
            market_event.symbol = owner_.symbols_[i];
            if (i < dto->trade_klines_by_id.size() && dto->trade_klines_by_id[i].has_value()) {
                market_event.has_kline = true;
                market_event.kline = dto->trade_klines_by_id[i].value();
                owner_.market_update_by_id_.market.trade_klines_by_id[i] = &dto->trade_klines_by_id[i].value();
                has_trade_kline_for_update = true;
            }

            double mark = 0.0;
            const bool has_mark = (i < dto->mark_klines_by_id.size() && dto->mark_klines_by_id[i].has_value());
            if (has_mark) {
                mark = dto->mark_klines_by_id[i]->ClosePrice;
                owner_.last_mark_by_symbol_[i] = mark;
                owner_.last_mark_ts_by_symbol_[i] = ts;
                owner_.has_last_mark_[i] = 1;
                owner_.market_update_by_id_.reference.has_mark_price_by_id[i] = 1;
                owner_.market_update_by_id_.reference.mark_price_by_id[i] = mark;
            }
            market_event.has_mark_price = has_mark;
            market_event.mark_price = has_mark ? mark : 0.0;
            if (i < owner_.last_mark_source_by_symbol_.size()) {
                market_event.mark_price_source = owner_.last_mark_source_by_symbol_[i];
                owner_.market_update_by_id_.reference.mark_price_source_by_id[i] = owner_.last_mark_source_by_symbol_[i];
            }

            double index = 0.0;
            const bool has_index = (i < dto->index_klines_by_id.size() && dto->index_klines_by_id[i].has_value());
            if (has_index) {
                index = dto->index_klines_by_id[i]->ClosePrice;
                owner_.last_index_by_symbol_[i] = index;
                owner_.last_index_ts_by_symbol_[i] = ts;
                owner_.has_last_index_[i] = 1;
                owner_.market_update_by_id_.reference.has_index_price_by_id[i] = 1;
                owner_.market_update_by_id_.reference.index_price_by_id[i] = index;
            }
            market_event.has_index_price = has_index;
            market_event.index_price = has_index ? index : 0.0;
            if (i < owner_.last_index_source_by_symbol_.size()) {
                market_event.index_price_source = owner_.last_index_source_by_symbol_[i];
                owner_.market_update_by_id_.reference.index_price_source_by_id[i] = owner_.last_index_source_by_symbol_[i];
            }

            if (i < dto->funding_by_id.size() && dto->funding_by_id[i].has_value()) {
                owner_.market_update_by_id_.funding.has_funding_by_id[i] = 1;
                owner_.market_update_by_id_.funding.funding_rate_by_id[i] = dto->funding_by_id[i]->Rate;
                owner_.market_update_by_id_.funding.funding_time_by_id[i] = dto->funding_by_id[i]->FundingTime;
            }

            SimulatorRiskOverlayEngine::ConsumeSymbolPrices(owner_,
                i,
                has_mark,
                mark,
                has_index,
                index,
                max_abs_basis_bps,
                warning_symbols,
                stress_symbols);
            market_events.emplace_back(std::move(market_event));
        }
        SimulatorRiskOverlayEngine::FinalizeStep(owner_, max_abs_basis_bps, warning_symbols, stress_symbols);
    };

    auto update_positions = [&]() {
        if (!has_trade_kline_for_update) {
            return;
        }
        std::lock_guard<std::mutex> lk(owner_.account_mtx_);
        if (owner_.account_engine_) {
            owner_.account_engine_->update_positions_by_id(owner_.market_update_by_id_);
        }
    };

    auto apply_basis_risk_guard = [&]() {
        std::vector<std::pair<std::string, double>> leverage_caps;
        SimulatorRiskOverlayEngine::CollectLeverageCaps(owner_, leverage_caps);
        if (leverage_caps.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lk(owner_.account_mtx_);
        if (!owner_.account_engine_) {
            return;
        }
        for (const auto& [symbol, cap] : leverage_caps) {
            const double current = owner_.account_engine_->perp.get_symbol_leverage(symbol);
            if (std::isfinite(current) && current > cap) {
                owner_.account_engine_->perp.set_symbol_leverage(symbol, cap);
            }
        }
    };

    auto apply_funding = [&]() {
        std::scoped_lock<std::mutex, std::mutex> lk(owner_.account_mtx_, owner_.state_mtx_);
        owner_.collect_funding_events_unlocked_(ts, funding_events);
    };

    if (apply_timing == FundingApplyTiming::BeforeMatching) {
        apply_funding();
        build_market();
        apply_basis_risk_guard();
        update_positions();
    }
    else {
        build_market();
        apply_basis_risk_guard();
        update_positions();
        apply_funding();
    }

    {
        std::lock_guard<std::mutex> lk(owner_.account_mtx_);
        cur_ver = owner_.account_engine_->get_state_version();
        curP = make_pooled<std::vector<dto::Position>>(owner_.account_engine_->get_all_positions());
        curO = make_pooled<std::vector<dto::Order>>(owner_.account_engine_->get_all_open_orders());
        perp_balance = owner_.account_engine_->get_perp_balance();
        spot_balance = owner_.account_engine_->get_spot_balance();
        total_cash_balance = owner_.account_engine_->get_total_cash_balance();
        spot_inventory_value = curP ? spot_inventory_value_from_positions(*curP) : 0.0;
        fill_events = owner_.account_engine_->drain_fill_events();
    }

    step_log_ctx.ts_local = now_ms();

    QTR_TRACE("ex", "market_channel Send begin");
    owner_.market_channel->Send(dto);
    QTR_TRACE("ex", "market_channel Send end");

    bool pos_changed = false;
    bool ord_changed = false;
    if (cur_ver != owner_.last_account_version_ && curP && curO) {
        const bool pos_unchanged = owner_.last_pos_snapshot_
            ? BinanceExchange::vec_equal(*curP, *owner_.last_pos_snapshot_)
            : curP->empty();
        const bool ord_unchanged = owner_.last_ord_snapshot_
            ? BinanceExchange::vec_equal(*curO, *owner_.last_ord_snapshot_)
            : curO->empty();

        if (!pos_unchanged) {
            QTR_TRACE("ex", "position_channel Send");
            owner_.position_channel->Send(*curP);
            pos_changed = true;
        }

        if (!ord_unchanged) {
            QTR_TRACE("ex", "order_channel Send");
            owner_.order_channel->Send(*curO);
            ord_changed = true;
        }
    }

    if (owner_.logger) {
        LogTask task;
        task.ctx = step_log_ctx;
        task.market_events = std::move(market_events);
        task.positions = curP;
        task.orders = curO;
        task.funding_events = std::move(funding_events);
        task.perp_balance = perp_balance;
        task.spot_balance = spot_balance;
        task.total_cash_balance = total_cash_balance;
        task.spot_inventory_value = spot_inventory_value;
        task.fill_events = std::move(fill_events);
        task.cur_ver = cur_ver;
        owner_.publish_log_task_(std::move(task));
    }

    if (cur_ver != owner_.last_account_version_) {
        if (pos_changed) {
            owner_.last_pos_snapshot_ = curP;
        }
        if (ord_changed) {
            owner_.last_ord_snapshot_ = curO;
        }
        owner_.last_account_version_ = cur_ver;
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
    case Code::UnknownSymbol:
        return { -1121, "Invalid symbol." };
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

void BinanceExchange::set_mark_index_basis_thresholds_bps(double warning_bps, double stress_bps)
{
    const double warning = std::max(0.0, warning_bps);
    const double stress = std::max(warning, stress_bps);
    std::lock_guard<std::mutex> lk(state_mtx_);
    simulator_risk_overlay_.mark_index_warning_bps = warning;
    simulator_risk_overlay_.mark_index_stress_bps = stress;
}

void BinanceExchange::set_basis_risk_leverage_caps(double warning_cap, double stress_cap)
{
    std::lock_guard<std::mutex> lk(state_mtx_);
    simulator_risk_overlay_.basis_warning_leverage_cap = (warning_cap >= 1.0) ? warning_cap : 0.0;
    simulator_risk_overlay_.basis_stress_leverage_cap = (stress_cap >= 1.0) ? stress_cap : 0.0;
}

void BinanceExchange::set_simulator_risk_overlay_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lk(state_mtx_);
    simulator_risk_overlay_.enabled = enabled;
    if (!enabled) {
        simulator_risk_overlay_.warning_symbols = 0;
        simulator_risk_overlay_.stress_symbols = 0;
        std::fill(simulator_risk_overlay_.warning_active_by_symbol.begin(),
            simulator_risk_overlay_.warning_active_by_symbol.end(),
            static_cast<uint8_t>(0));
        std::fill(simulator_risk_overlay_.stress_active_by_symbol.begin(),
            simulator_risk_overlay_.stress_active_by_symbol.end(),
            static_cast<uint8_t>(0));
    }
}

bool BinanceExchange::simulator_risk_overlay_enabled() const
{
    std::lock_guard<std::mutex> lk(state_mtx_);
    return simulator_risk_overlay_.enabled;
}

void BinanceExchange::set_basis_risk_guard_enabled(bool enabled)
{
    set_simulator_risk_overlay_enabled(enabled);
}

bool BinanceExchange::basis_risk_guard_enabled() const
{
    return simulator_risk_overlay_enabled();
}

void BinanceExchange::set_basis_stress_blocks_opening_orders(bool enabled)
{
    std::lock_guard<std::mutex> lk(state_mtx_);
    simulator_risk_overlay_.basis_stress_blocks_opening_orders = enabled;
}

bool BinanceExchange::basis_stress_blocks_opening_orders() const
{
    std::lock_guard<std::mutex> lk(state_mtx_);
    return simulator_risk_overlay_.basis_stress_blocks_opening_orders;
}

/// @brief Close the simulator: drain CSVs and close all channels.
/// @details Advances each symbol's cursor to the end before closing.
void BinanceExchange::close()
{
    {
        std::lock_guard<std::mutex> state_lk(state_mtx_);
        for (size_t i = 0; i < md_.size(); ++i) {
            cursor_[i] = (i < kline_window_end_idx_.size()) ? kline_window_end_idx_[i] : md_[i].get_klines_count();
            has_next_ts_[i] = 0;
        }
    }

	IExchange<MultiKlinePtr>::close();
}

bool BinanceExchange::MarketReplayEngine::NextTimestamp(BinanceExchange& owner, uint64_t& ts)
{
    uint64_t next_kline_ts = std::numeric_limits<uint64_t>::max();
    bool has_next_kline = false;

    // Pop stale heap entries until top matches current per-symbol next_ts.
    while (!owner.next_ts_heap_.empty()) {
        const auto top = owner.next_ts_heap_.top();
        if (top.sym_id < owner.next_ts_by_symbol_.size() &&
            owner.has_next_ts_[top.sym_id] &&
            owner.next_ts_by_symbol_[top.sym_id] == top.ts) {
            next_kline_ts = top.ts;
            has_next_kline = true;
            break;
        }
        owner.next_ts_heap_.pop();
    }

    uint64_t next_funding_ts = std::numeric_limits<uint64_t>::max();
    bool has_next_funding = false;
    const size_t funding_count = std::min(owner.funding_md_.size(), owner.funding_cursor_.size());
    for (size_t i = 0; i < funding_count; ++i) {
        if (!owner.has_funding_[i] || !owner.funding_md_[i]) {
            continue;
        }
        const auto& data = *owner.funding_md_[i];
        const size_t cur = owner.funding_cursor_[i];
        const size_t end_idx = (i < owner.funding_window_end_idx_.size())
            ? owner.funding_window_end_idx_[i]
            : data.get_count();
        if (cur >= end_idx) {
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

void BinanceExchange::MarketReplayEngine::BuildMultiKline(BinanceExchange& owner,
    uint64_t ts,
    MultiKlineDto& out)
{
    out.Timestamp = ts;
    out.symbols = owner.symbols_shared_;
    out.trade_klines_by_id.clear();
    out.trade_klines_by_id.resize(owner.symbols_.size());
    out.mark_klines_by_id.clear();
    out.mark_klines_by_id.resize(owner.symbols_.size());
    out.index_klines_by_id.clear();
    out.index_klines_by_id.resize(owner.symbols_.size());
    out.funding_by_id.clear();
    out.funding_by_id.resize(owner.symbols_.size());

    for (size_t i = 0; i < owner.md_.size(); ++i) {
        auto& data = owner.md_[i];
        size_t idx = owner.cursor_[i];
        const size_t end_idx = (i < owner.kline_window_end_idx_.size())
            ? owner.kline_window_end_idx_[i]
            : data.get_klines_count();
        if (idx < end_idx &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            out.trade_klines_by_id[i] = k;
            owner.last_close_by_symbol_[i] = k.ClosePrice;
            owner.last_close_ts_by_symbol_[i] = k.Timestamp;
            owner.has_last_close_[i] = 1;
            ++owner.cursor_[i];

            // Advance this symbol in the multiway merge heap.
            if (owner.cursor_[i] < end_idx) {
                const uint64_t next_ts = data.get_kline(owner.cursor_[i]).Timestamp;
                owner.next_ts_by_symbol_[i] = next_ts;
                owner.has_next_ts_[i] = 1;
                owner.next_ts_heap_.push(HeapItem{ next_ts, i });
            }
            else {
                owner.has_next_ts_[i] = 0;
            }
        }

        if (i < owner.has_last_funding_.size() && owner.has_last_funding_[i]) {
            out.funding_by_id[i] = FundingRateDto(
                owner.last_funding_time_by_symbol_[i],
                owner.last_funding_rate_by_symbol_[i]);
        }

        double mark = 0.0;
        ReferencePriceSource mark_source = ReferencePriceSource::None;
        if (owner.resolve_mark_price_with_source_(i, ts, mark, mark_source)) {
            out.mark_klines_by_id[i] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, mark);
        }
        if (i < owner.last_mark_source_by_symbol_.size()) {
            owner.last_mark_source_by_symbol_[i] = static_cast<int32_t>(mark_source);
        }

        double index = 0.0;
        ReferencePriceSource index_source = ReferencePriceSource::None;
        if (owner.resolve_index_price_with_source_(i, ts, index, index_source)) {
            out.index_klines_by_id[i] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, index);
        }
        if (i < owner.last_index_source_by_symbol_.size()) {
            owner.last_index_source_by_symbol_[i] = static_cast<int32_t>(index_source);
        }
    }
}

/// @brief Determine the next global timestamp to emit.
/// @param[out] ts  The minimum upcoming timestamp among all symbols.
/// @return true if at least one symbol has data remaining.
bool BinanceExchange::next_timestamp(uint64_t& ts)
{
    return MarketReplayEngine::NextTimestamp(*this, ts);
}

/// @brief Build and send a MultiKlineDto for timestamp `ts`.
/// @param ts   Global timestamp to align on.
/// @param out  DTO to populate with per-symbol optional TradeKlineDto.
void BinanceExchange::build_multikline(uint64_t ts,
    MultiKlineDto& out)
{
    MarketReplayEngine::BuildMultiKline(*this, ts, out);
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
        logger->Log(account_module_id_, make_log_payload<dto::AccountLog>(
            dto::AccountLog{
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
    const std::vector<LogTask::DomainMarketEvent>& market_events,
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
        market_event_buffer_.reserve(market_events.size());
        for (const auto& captured : market_events) {
            MarketEventDto e;
            e.symbol = captured.symbol;
            if (captured.has_kline) {
                const auto& k = captured.kline;
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
            e.has_mark_price = captured.has_mark_price;
            e.mark_price = captured.mark_price;
            e.mark_price_source = captured.mark_price_source;
            e.has_index_price = captured.has_index_price;
            e.index_price = captured.index_price;
            e.index_price_source = captured.index_price_source;
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
            e.fee_asset = f.fee_asset;
            e.fee_native = f.fee_native;
            e.fee_quote_equiv = f.fee_quote_equiv;
            e.spot_cash_delta = f.spot_cash_delta;
            e.spot_inventory_delta = f.spot_inventory_delta;
            e.commission_model_source = f.commission_model_source;
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
        e.fee_asset = static_cast<int32_t>(Account::CommissionAsset::None);
        e.fee_native = 0.0;
        e.fee_quote_equiv = 0.0;
        e.spot_cash_delta = 0.0;
        e.spot_inventory_delta = 0.0;
        e.commission_model_source = static_cast<int32_t>(Account::CommissionModelSource::None);
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
                e.close_position = o.close_position;
                e.quote_order_qty = o.quote_order_qty;
                e.qty = o.quantity;
                e.price = o.price;
                e.exec_qty = exec_qty;
                e.exec_price = exec_price;
                e.remaining_qty = remaining_qty;
                e.closing_position_id = o.closing_position_id;
                e.is_taker = false;
                e.fee = 0.0;
                e.fee_rate = 0.0;
                e.fee_asset = static_cast<int32_t>(Account::CommissionAsset::None);
                e.fee_native = 0.0;
                e.fee_quote_equiv = 0.0;
                e.spot_cash_delta = 0.0;
                e.spot_inventory_delta = 0.0;
                e.commission_model_source = static_cast<int32_t>(Account::CommissionModelSource::None);
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
                e.close_position = f.close_position;
                e.quote_order_qty = f.quote_order_qty;
                e.qty = f.order_qty;
                e.price = f.order_price;
                e.exec_qty = f.exec_qty;
                e.exec_price = f.exec_price;
                e.remaining_qty = f.remaining_qty;
                e.closing_position_id = f.closing_position_id;
                e.is_taker = f.is_taker;
                e.fee = f.fee;
                e.fee_rate = f.fee_rate;
                e.fee_asset = f.fee_asset;
                e.fee_native = f.fee_native;
                e.fee_quote_equiv = f.fee_quote_equiv;
                e.spot_cash_delta = f.spot_cash_delta;
                e.spot_inventory_delta = f.spot_inventory_delta;
                e.commission_model_source = f.commission_model_source;
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

        size_t end = data.upper_bound_ts(ts);
        if (i < funding_window_end_idx_.size()) {
            end = std::min(end, funding_window_end_idx_[i]);
        }
        while (cur < end) {
            const auto& fr = data.get_funding(cur);
            if (i < has_last_funding_.size()) {
                has_last_funding_[i] = 1;
                last_funding_rate_by_symbol_[i] = fr.Rate;
                last_funding_time_by_symbol_[i] = fr.FundingTime;
            }

            double price = 0.0;
            bool has_price = false;
            ReferencePriceSource mark_source = ReferencePriceSource::None;
            if (fr.MarkPrice.has_value()) {
                price = fr.MarkPrice.value();
                has_price = true;
                mark_source = ReferencePriceSource::Raw;
            }
            else if (interpolate_mark_price_(i, fr.FundingTime, price)) {
                has_price = true;
                mark_source = ReferencePriceSource::Interpolated;
            }

            std::vector<Account::FundingApplyResult> applied;
            if (has_price && account_engine_) {
                applied = account_engine_->apply_funding(symbols_[i], fr.FundingTime, fr.Rate, price);
            }
            else if (!has_price) {
                ++funding_skipped_no_mark_total_;
                FundingEventDto skip;
                skip.symbol = symbols_[i];
                if (account_engine_) {
                    skip.instrument_type = static_cast<int32_t>(account_engine_->get_instrument_spec(symbols_[i]).type);
                }
                skip.funding_time = fr.FundingTime;
                skip.rate = fr.Rate;
                skip.has_mark_price = false;
                skip.mark_price = 0.0;
                skip.mark_price_source = static_cast<int32_t>(ReferencePriceSource::None);
                skip.skip_reason = 1;
                skip.position_id = -1;
                skip.is_long = false;
                skip.quantity = 0.0;
                skip.funding = 0.0;
                out.emplace_back(std::move(skip));
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
                e.mark_price_source = static_cast<int32_t>(mark_source);
                e.skip_reason = 0;
                e.position_id = static_cast<int64_t>(r.position_id);
                e.is_long = r.is_long;
                e.quantity = r.quantity;
                e.funding = r.funding;
                out.emplace_back(std::move(e));
            }
            funding_applied_events_total_ += applied.size();

            ++cur;
        }
    }
}

namespace {

bool interpolate_close_price_from_market_data_(const MarketData& data,
    uint64_t ts,
    uint64_t max_age_ms,
    double& out_price)
{
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
        if (max_age_ms > 0) {
            if (ts < prev.Timestamp || (ts - prev.Timestamp) > max_age_ms) {
                return false;
            }
        }
        out_price = prev.ClosePrice;
        return std::isfinite(out_price) && out_price > 0.0;
    }
    if (has_next) {
        const auto& next = data.get_kline(lo);
        if (max_age_ms > 0) {
            if (next.Timestamp < ts || (next.Timestamp - ts) > max_age_ms) {
                return false;
            }
        }
        out_price = next.ClosePrice;
        return std::isfinite(out_price) && out_price > 0.0;
    }
    return false;
}

bool exact_close_price_from_market_data_(const MarketData& data, uint64_t ts, double& out_price)
{
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
    if (lo >= n || data.get_kline(lo).Timestamp != ts) {
        return false;
    }

    out_price = data.get_kline(lo).ClosePrice;
    return std::isfinite(out_price) && out_price > 0.0;
}

} // namespace

bool BinanceExchange::interpolate_mark_price_(size_t sym_id, uint64_t ts, double& out_price) const
{
    if (sym_id >= md_.size()) {
        return false;
    }
    if (sym_id >= mark_md_.size() ||
        sym_id >= has_mark_md_.size() ||
        !has_mark_md_[sym_id] ||
        !mark_md_[sym_id]) {
        return false;
    }

    return interpolate_close_price_from_market_data_(*mark_md_[sym_id], ts, 0, out_price);
}

bool BinanceExchange::interpolate_index_price_(size_t sym_id, uint64_t ts, double& out_price) const
{
    if (sym_id >= md_.size()) {
        return false;
    }
    if (sym_id >= index_md_.size() ||
        sym_id >= has_index_md_.size() ||
        !has_index_md_[sym_id] ||
        !index_md_[sym_id]) {
        return false;
    }

    return interpolate_close_price_from_market_data_(*index_md_[sym_id], ts, 0, out_price);
}

bool BinanceExchange::resolve_mark_price_with_source_(size_t sym_id,
    uint64_t ts,
    double& out_price,
    ReferencePriceSource& out_source) const
{
    out_source = ReferencePriceSource::None;
    out_price = 0.0;
    if (sym_id >= mark_md_.size() ||
        sym_id >= has_mark_md_.size() ||
        !has_mark_md_[sym_id] ||
        !mark_md_[sym_id]) {
        return false;
    }

    if (exact_close_price_from_market_data_(*mark_md_[sym_id], ts, out_price)) {
        out_source = ReferencePriceSource::Raw;
        return true;
    }
    if (interpolate_mark_price_(sym_id, ts, out_price)) {
        out_source = ReferencePriceSource::Interpolated;
        return true;
    }
    return false;
}

bool BinanceExchange::resolve_index_price_with_source_(size_t sym_id,
    uint64_t ts,
    double& out_price,
    ReferencePriceSource& out_source) const
{
    out_source = ReferencePriceSource::None;
    out_price = 0.0;
    if (sym_id >= index_md_.size() ||
        sym_id >= has_index_md_.size() ||
        !has_index_md_[sym_id] ||
        !index_md_[sym_id]) {
        return false;
    }

    if (exact_close_price_from_market_data_(*index_md_[sym_id], ts, out_price)) {
        out_source = ReferencePriceSource::Raw;
        return true;
    }
    if (interpolate_index_price_(sym_id, ts, out_price)) {
        out_source = ReferencePriceSource::Interpolated;
        return true;
    }
    return false;
}

void BinanceExchange::SimulatorRiskOverlayEngine::BeginStep(BinanceExchange& owner)
{
    for (size_t i = 0; i < owner.symbols_.size(); ++i) {
        if (i < owner.has_last_mark_index_basis_.size()) {
            owner.has_last_mark_index_basis_[i] = 0;
        }
        if (i < owner.simulator_risk_overlay_.warning_active_by_symbol.size()) {
            owner.simulator_risk_overlay_.warning_active_by_symbol[i] = 0;
        }
        if (i < owner.simulator_risk_overlay_.stress_active_by_symbol.size()) {
            owner.simulator_risk_overlay_.stress_active_by_symbol[i] = 0;
        }
    }
}

void BinanceExchange::SimulatorRiskOverlayEngine::ConsumeSymbolPrices(
    BinanceExchange& owner,
    size_t sym_id,
    bool has_mark,
    double mark,
    bool has_index,
    double index,
    double& max_abs_basis_bps,
    uint32_t& warning_symbols,
    uint32_t& stress_symbols)
{
    if (!has_mark || !has_index || !std::isfinite(index) || std::abs(index) <= 1e-12) {
        return;
    }

    const double basis_bps = ((mark - index) / index) * 10000.0;
    if (!std::isfinite(basis_bps)) {
        return;
    }

    if (sym_id < owner.last_mark_index_basis_bps_by_symbol_.size()) {
        owner.last_mark_index_basis_bps_by_symbol_[sym_id] = basis_bps;
    }
    if (sym_id < owner.has_last_mark_index_basis_.size()) {
        owner.has_last_mark_index_basis_[sym_id] = 1;
    }

    const double abs_basis_bps = std::abs(basis_bps);
    max_abs_basis_bps = std::max(max_abs_basis_bps, abs_basis_bps);
    if (!owner.simulator_risk_overlay_.enabled) {
        return;
    }

    if (abs_basis_bps >= owner.simulator_risk_overlay_.mark_index_stress_bps) {
        if (sym_id < owner.simulator_risk_overlay_.stress_active_by_symbol.size()) {
            owner.simulator_risk_overlay_.stress_active_by_symbol[sym_id] = 1;
        }
        ++stress_symbols;
    }
    else if (abs_basis_bps >= owner.simulator_risk_overlay_.mark_index_warning_bps) {
        if (sym_id < owner.simulator_risk_overlay_.warning_active_by_symbol.size()) {
            owner.simulator_risk_overlay_.warning_active_by_symbol[sym_id] = 1;
        }
        ++warning_symbols;
    }
}

void BinanceExchange::SimulatorRiskOverlayEngine::FinalizeStep(BinanceExchange& owner,
    double max_abs_basis_bps,
    uint32_t warning_symbols,
    uint32_t stress_symbols)
{
    owner.last_mark_index_max_abs_basis_bps_ = max_abs_basis_bps;
    owner.simulator_risk_overlay_.warning_symbols = warning_symbols;
    owner.simulator_risk_overlay_.stress_symbols = stress_symbols;
}

void BinanceExchange::SimulatorRiskOverlayEngine::CollectLeverageCaps(
    const BinanceExchange& owner,
    std::vector<std::pair<std::string, double>>& out)
{
    std::lock_guard<std::mutex> state_lk(owner.state_mtx_);
    if (!owner.simulator_risk_overlay_.enabled) {
        return;
    }

    out.reserve(owner.symbols_.size());
    for (size_t i = 0; i < owner.symbols_.size(); ++i) {
        double cap = 0.0;
        if (i < owner.simulator_risk_overlay_.stress_active_by_symbol.size() &&
            owner.simulator_risk_overlay_.stress_active_by_symbol[i] &&
            owner.simulator_risk_overlay_.basis_stress_leverage_cap >= 1.0) {
            cap = owner.simulator_risk_overlay_.basis_stress_leverage_cap;
        }
        else if (i < owner.simulator_risk_overlay_.warning_active_by_symbol.size() &&
            owner.simulator_risk_overlay_.warning_active_by_symbol[i] &&
            owner.simulator_risk_overlay_.basis_warning_leverage_cap >= 1.0) {
            cap = owner.simulator_risk_overlay_.basis_warning_leverage_cap;
        }
        if (cap > 0.0) {
            out.emplace_back(owner.symbols_[i], cap);
        }
    }
}

bool BinanceExchange::SimulatorRiskOverlayEngine::IsOpeningBlockedByStress(
    const BinanceExchange& owner,
    Account& acc,
    const std::string& symbol,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    if (reduce_only) {
        return false;
    }

    size_t sym_id = owner.symbols_.size();
    bool stress_active = false;
    bool guard_enabled = false;
    bool blocks_opening = false;
    {
        std::lock_guard<std::mutex> state_lk(owner.state_mtx_);
        guard_enabled = owner.simulator_risk_overlay_.enabled;
        blocks_opening = owner.simulator_risk_overlay_.basis_stress_blocks_opening_orders;
        for (size_t i = 0; i < owner.symbols_.size(); ++i) {
            if (owner.symbols_[i] == symbol) {
                sym_id = i;
                break;
            }
        }
        if (sym_id < owner.simulator_risk_overlay_.stress_active_by_symbol.size()) {
            stress_active = owner.simulator_risk_overlay_.stress_active_by_symbol[sym_id] != 0;
        }
    }

    if (!guard_enabled || !blocks_opening || !stress_active) {
        return false;
    }

    const auto& positions = acc.get_all_positions();
    if (position_side == QTrading::Dto::Trading::PositionSide::Long) {
        return side != QTrading::Dto::Trading::OrderSide::Sell;
    }
    if (position_side == QTrading::Dto::Trading::PositionSide::Short) {
        return side != QTrading::Dto::Trading::OrderSide::Buy;
    }

    double net_qty = 0.0;
    bool has_perp_position = false;
    for (const auto& p : positions) {
        if (p.symbol != symbol ||
            p.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            p.quantity <= 0.0) {
            continue;
        }
        has_perp_position = true;
        net_qty += p.is_long ? p.quantity : -p.quantity;
    }

    if (!has_perp_position) {
        return true;
    }
    if (net_qty > 1e-12) {
        return side != QTrading::Dto::Trading::OrderSide::Sell;
    }
    if (net_qty < -1e-12) {
        return side != QTrading::Dto::Trading::OrderSide::Buy;
    }
    return true;
}

bool BinanceExchange::perp_opening_blocked_by_basis_stress_account_locked_(
    Account& acc,
    const std::string& symbol,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only) const
{
    return SimulatorRiskOverlayEngine::IsOpeningBlockedByStress(
        *this, acc, symbol, side, position_side, reduce_only);
}

void BinanceExchange::FillStatusSnapshot(StatusSnapshot& out) const
{
    StatusSnapshotBuilder::Fill(*this, out);
}

void BinanceExchange::StatusSnapshotBuilder::Fill(const BinanceExchange& owner,
    BinanceExchange::StatusSnapshot& out)
{
    double uncertainty_bps = 0.0;
    double mark_index_diag_bps = 0.0;
    {
        std::lock_guard<std::mutex> lk(owner.account_mtx_);
        uncertainty_bps = owner.uncertainty_band_bps_;
        if (owner.account_engine_) {
            const auto perp_bal = owner.account_engine_->get_perp_balance();
            const auto spot_bal = owner.account_engine_->get_spot_balance();
            const auto positions = owner.account_engine_->get_all_positions();
            const double spot_inventory_value = spot_inventory_value_from_positions(positions);
            const double spot_ledger_value = spot_bal.WalletBalance + spot_inventory_value;
            const double total_cash_balance = owner.account_engine_->get_total_cash_balance();
            const double total_ledger_value = perp_bal.Equity + spot_ledger_value;

            out.wallet_balance = perp_bal.WalletBalance;
            out.margin_balance = perp_bal.MarginBalance;
            out.available_balance = perp_bal.AvailableBalance;
            out.unrealized_pnl = perp_bal.UnrealizedPnl;
            out.total_unrealized_pnl = owner.account_engine_->total_unrealized_pnl();
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
        std::lock_guard<std::mutex> state_lk(owner.state_mtx_);
        mark_index_diag_bps = std::max(0.0, owner.last_mark_index_max_abs_basis_bps_);
        out.ts_exchange = owner.last_step_ts_;
        out.progress_pct = owner.progress_pct_unlocked_();
        out.basis_warning_symbols = owner.simulator_risk_overlay_.warning_symbols;
        out.basis_stress_symbols = owner.simulator_risk_overlay_.stress_symbols;
        out.prices.clear();
        out.prices.reserve(owner.symbols_.size());
        for (size_t i = 0; i < owner.symbols_.size(); ++i) {
            StatusSnapshot::PriceSnapshot snap;
            snap.symbol = owner.symbols_[i];
            snap.trade_price = owner.last_close_by_symbol_[i];
            snap.has_trade_price = owner.has_last_close_[i] != 0;
            snap.price = snap.trade_price;
            snap.has_price = snap.has_trade_price;
            snap.mark_price = (i < owner.last_mark_by_symbol_.size()) ? owner.last_mark_by_symbol_[i] : 0.0;
            snap.has_mark_price = (i < owner.has_last_mark_.size()) && (owner.has_last_mark_[i] != 0);
            snap.mark_price_source = (i < owner.last_mark_source_by_symbol_.size())
                ? owner.last_mark_source_by_symbol_[i]
                : static_cast<int32_t>(ReferencePriceSource::None);
            snap.index_price = (i < owner.last_index_by_symbol_.size()) ? owner.last_index_by_symbol_[i] : 0.0;
            snap.has_index_price = (i < owner.has_last_index_.size()) && (owner.has_last_index_[i] != 0);
            snap.index_price_source = (i < owner.last_index_source_by_symbol_.size())
                ? owner.last_index_source_by_symbol_[i]
                : static_cast<int32_t>(ReferencePriceSource::None);
            out.prices.emplace_back(std::move(snap));
        }
        out.funding_applied_events = owner.funding_applied_events_total_;
        out.funding_skipped_no_mark = owner.funding_skipped_no_mark_total_;
    }
    out.basis_stress_blocked_orders = owner.simulator_risk_overlay_.stress_blocked_orders.load(std::memory_order_relaxed);
    out.uncertainty_band_bps = std::max(0.0, uncertainty_bps) + std::max(0.0, mark_index_diag_bps);
    const double total_band = out.uncertainty_band_bps / 10000.0;
    out.total_ledger_value_conservative = out.total_ledger_value_base * (1.0 - total_band);
    out.total_ledger_value_optimistic = out.total_ledger_value_base * (1.0 + total_band);
}

double BinanceExchange::progress_pct_() const
{
    std::lock_guard<std::mutex> state_lk(state_mtx_);
    return progress_pct_unlocked_();
}

double BinanceExchange::progress_pct_unlocked_() const
{
    if (kline_window_end_idx_.empty()) {
        return 0.0;
    }
    double min_ratio = 1.0;
    bool has_count = false;
    const size_t count = std::min(
        std::min(kline_window_begin_idx_.size(), kline_window_end_idx_.size()),
        cursor_.size());
    for (size_t i = 0; i < count; ++i) {
        const auto begin_idx = kline_window_begin_idx_[i];
        const auto end_idx = kline_window_end_idx_[i];
        if (end_idx <= begin_idx) {
            continue;
        }
        const auto total = end_idx - begin_idx;
        if (total == 0) {
            continue;
        }
        has_count = true;
        const size_t cur_idx = std::min(cursor_[i], end_idx);
        const size_t progressed = (cur_idx > begin_idx) ? (cur_idx - begin_idx) : 0;
        double ratio = static_cast<double>(progressed) / static_cast<double>(total);
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
        x.close_position == y.close_position &&
        std::abs(x.quote_order_qty - y.quote_order_qty) <= 1e-12 &&
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

