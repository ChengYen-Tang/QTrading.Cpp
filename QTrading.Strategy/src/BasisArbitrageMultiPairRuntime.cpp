#include "Strategy/BasisArbitrageMultiPairRuntime.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Signal/SignalDecision.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace {

constexpr double kAllocatorScoreFloor = 1e-12;

QTrading::Execution::ExecutionSignalStatus ToExecutionStatus(
    QTrading::Signal::SignalStatus status)
{
    switch (status) {
    case QTrading::Signal::SignalStatus::Active:
        return QTrading::Execution::ExecutionSignalStatus::Active;
    case QTrading::Signal::SignalStatus::Cooldown:
        return QTrading::Execution::ExecutionSignalStatus::Cooldown;
    case QTrading::Signal::SignalStatus::Inactive:
    default:
        return QTrading::Execution::ExecutionSignalStatus::Inactive;
    }
}

QTrading::Execution::ExecutionUrgency ToExecutionUrgency(
    QTrading::Signal::SignalUrgency urgency)
{
    switch (urgency) {
    case QTrading::Signal::SignalUrgency::High:
        return QTrading::Execution::ExecutionUrgency::High;
    case QTrading::Signal::SignalUrgency::Medium:
        return QTrading::Execution::ExecutionUrgency::Medium;
    case QTrading::Signal::SignalUrgency::Low:
    default:
        return QTrading::Execution::ExecutionUrgency::Low;
    }
}

QTrading::Execution::ExecutionSignal ToExecutionSignal(
    const QTrading::Signal::SignalDecision& signal)
{
    return QTrading::Execution::ExecutionSignal{
        signal.ts_ms,
        signal.symbol,
        signal.strategy,
        signal.strategy_kind,
        ToExecutionStatus(signal.status),
        signal.confidence,
        ToExecutionUrgency(signal.urgency)
    };
}

bool EndsWith(const std::string& value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StripSuffix(const std::string& value, std::string_view suffix)
{
    if (!EndsWith(value, suffix)) {
        return value;
    }
    return value.substr(0, value.size() - suffix.size());
}

QTrading::Signal::SignalDecision MakeInactiveBasisSignal(std::uint64_t ts_ms)
{
    QTrading::Signal::SignalDecision out{};
    out.ts_ms = ts_ms;
    out.strategy = "basis_arbitrage";
    out.strategy_kind = QTrading::Contracts::StrategyKind::BasisArbitrage;
    out.status = QTrading::Signal::SignalStatus::Inactive;
    out.confidence = 0.0;
    out.urgency = QTrading::Signal::SignalUrgency::Low;
    return out;
}

} // namespace

namespace QTrading::Strategy {

BasisArbitrageMultiPairRuntime::PairRuntimeState::PairRuntimeState(
    std::string spot_symbol_in,
    std::string perp_symbol_in,
    QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
    QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg)
    : signal_engine([&]() {
        signal_cfg.spot_symbol = spot_symbol_in;
        signal_cfg.perp_symbol = perp_symbol_in;
        return QTrading::Signal::BasisArbitrageSignalEngine(std::move(signal_cfg));
    }())
    , intent_builder([&]() {
        intent_cfg.spot_symbol = spot_symbol_in;
        intent_cfg.perp_symbol = perp_symbol_in;
        return QTrading::Intent::BasisArbitrageIntentBuilder(std::move(intent_cfg));
    }())
{
}

BasisArbitrageMultiPairRuntime::~BasisArbitrageMultiPairRuntime()
{
    StopWorkers();
}

BasisArbitrageMultiPairRuntime::BasisArbitrageMultiPairRuntime(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    QTrading::Universe::IUniverseSelector& universe_selector,
    QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
    QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg,
    StrategyRuntimeConfig runtime_cfg,
    QTrading::Risk::SimpleRiskEngine& risk_engine,
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
    QTrading::Monitoring::SimpleMonitoring& monitoring,
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types)
    : exchange_(std::move(exchange))
    , universe_selector_(universe_selector)
    , base_signal_cfg_(std::move(signal_cfg))
    , base_intent_cfg_(std::move(intent_cfg))
    , runtime_cfg_(std::move(runtime_cfg))
    , risk_engine_(risk_engine)
    , execution_engine_(execution_engine)
    , monitoring_(monitoring)
    , execution_scheduler_()
    , execution_policy_()
    , execution_orchestrator_(execution_engine_, execution_scheduler_, execution_policy_)
    , exchange_gateway_(exchange_, std::move(instrument_types))
{
    runtime_cfg_.basis_multi_top_n = std::max<std::size_t>(1, runtime_cfg_.basis_multi_top_n);
    runtime_cfg_.basis_multi_shard_size = std::max<std::size_t>(1, runtime_cfg_.basis_multi_shard_size);
}

void BasisArbitrageMultiPairRuntime::InitializePairsIfNeeded(const MarketPtr& market)
{
    if (!pair_runtime_states_.empty() || !market || !market->symbols) {
        return;
    }

    const auto& symbols = *market->symbols;
    std::unordered_map<std::string, std::size_t> symbol_to_market_id;
    symbol_to_market_id.reserve(symbols.size());
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        symbol_to_market_id.emplace(symbols[i], i);
    }

    pair_static_infos_.reserve(symbols.size() / 2);
    pair_runtime_states_.reserve(symbols.size() / 2);
    symbol_to_pair_index_.reserve(symbols.size());

    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto& symbol = symbols[i];
        if (!EndsWith(symbol, "_SPOT")) {
            continue;
        }
        const auto raw_symbol = StripSuffix(symbol, "_SPOT");
        const auto perp_symbol = raw_symbol + "_PERP";
        const auto perp_it = symbol_to_market_id.find(perp_symbol);
        if (perp_it == symbol_to_market_id.end()) {
            continue;
        }
        const std::size_t pair_index = pair_runtime_states_.size();
        pair_static_infos_.push_back(PairStaticInfo{
            raw_symbol,
            symbol,
            perp_symbol
        });
        pair_runtime_states_.emplace_back(
            symbol,
            perp_symbol,
            base_signal_cfg_,
            base_intent_cfg_);
        symbol_to_pair_index_.emplace(symbol, pair_index);
        symbol_to_pair_index_.emplace(perp_symbol, pair_index);
    }
    RebuildShards();
    ranking_buffer_.reserve(pair_runtime_states_.size());
    active_pair_slots_.assign(pair_runtime_states_.size(), -1);
    active_pair_slot_touched_.reserve(runtime_cfg_.basis_multi_top_n);
    InitializeWorkersIfNeeded();
}

std::vector<BasisArbitrageMultiPairRuntime::PairSignalSnapshot>
BasisArbitrageMultiPairRuntime::BuildActivePairRanking(const MarketPtr& market)
{
    ranking_buffer_.clear();
    if (workers_.empty()) {
        for (const auto& shard : shards_) {
            CollectActivePairsInShard(market, shard, ranking_buffer_);
        }
    }
    else {
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            current_market_ = market;
            worker_exception_ = nullptr;
            completed_workers_ = 0;
            ++current_generation_;
        }
        worker_cv_.notify_all();

        std::unique_lock<std::mutex> lock(worker_mutex_);
        worker_done_cv_.wait(lock, [&]() {
            return completed_workers_ == workers_.size() || worker_exception_ != nullptr;
        });
        if (worker_exception_ != nullptr) {
            std::rethrow_exception(worker_exception_);
        }
        lock.unlock();

        for (const auto& worker : workers_) {
            ranking_buffer_.insert(
                ranking_buffer_.end(),
                worker.output.begin(),
                worker.output.end());
        }
    }

    const auto signal_rank_cmp =
        [&](const PairSignalSnapshot& lhs, const PairSignalSnapshot& rhs) {
            if (lhs.signal.allocator_score != rhs.signal.allocator_score) {
                return lhs.signal.allocator_score > rhs.signal.allocator_score;
            }
            if (lhs.signal.confidence != rhs.signal.confidence) {
                return lhs.signal.confidence > rhs.signal.confidence;
            }
            return pair_static_infos_[lhs.pair_index].raw_symbol <
                pair_static_infos_[rhs.pair_index].raw_symbol;
        };

    const std::size_t top_n =
        std::min<std::size_t>(runtime_cfg_.basis_multi_top_n, ranking_buffer_.size());
    if (top_n == 0) {
        return ranking_buffer_;
    }
    if (ranking_buffer_.size() > top_n) {
        std::nth_element(
            ranking_buffer_.begin(),
            ranking_buffer_.begin() + static_cast<std::ptrdiff_t>(top_n),
            ranking_buffer_.end(),
            signal_rank_cmp);
        ranking_buffer_.resize(top_n);
    }
    std::sort(ranking_buffer_.begin(), ranking_buffer_.end(), signal_rank_cmp);
    return ranking_buffer_;
}

void BasisArbitrageMultiPairRuntime::RebuildShards()
{
    shards_.clear();
    if (pair_runtime_states_.empty()) {
        return;
    }
    const std::size_t shard_size = runtime_cfg_.basis_multi_shard_size;
    const std::size_t pair_count = pair_runtime_states_.size();
    const std::size_t shard_count = (pair_count + shard_size - 1) / shard_size;
    shards_.reserve(shard_count);

    for (std::size_t begin = 0; begin < pair_count; begin += shard_size) {
        shards_.push_back(PairShard{
            begin,
            std::min(begin + shard_size, pair_count)
        });
    }
}

void BasisArbitrageMultiPairRuntime::InitializeWorkersIfNeeded()
{
    if (workers_initialized_) {
        return;
    }
    workers_initialized_ = true;

    if (shards_.size() <= 1) {
        return;
    }

    std::size_t worker_count = runtime_cfg_.basis_multi_worker_count;
    if (worker_count == 0) {
        const unsigned int hw = std::thread::hardware_concurrency();
        worker_count = (hw == 0) ? 1u : hw;
    }
    worker_count = std::min<std::size_t>(shards_.size(), worker_count);

    if (worker_count <= 1) {
        return;
    }

    workers_.resize(worker_count);
    for (std::size_t shard_index = 0; shard_index < shards_.size(); ++shard_index) {
        const std::size_t worker_index = shard_index % worker_count;
        workers_[worker_index].shard_indexes.push_back(shard_index);
    }
    for (auto& worker : workers_) {
        worker.output.reserve(pair_runtime_states_.size() / worker_count + 1);
    }
    for (std::size_t worker_index = 0; worker_index < workers_.size(); ++worker_index) {
        workers_[worker_index].thread =
            std::thread(&BasisArbitrageMultiPairRuntime::WorkerLoop, this, worker_index);
    }
}

void BasisArbitrageMultiPairRuntime::StopWorkers()
{
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_stop_requested_ = true;
    }
    worker_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    workers_.clear();
}

void BasisArbitrageMultiPairRuntime::WorkerLoop(std::size_t worker_index)
{
    std::size_t observed_generation = 0;
    for (;;) {
        MarketPtr market;
        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait(lock, [&]() {
                return worker_stop_requested_ || current_generation_ != observed_generation;
            });
            if (worker_stop_requested_) {
                return;
            }
            observed_generation = current_generation_;
            market = current_market_;
        }

        auto& worker = workers_[worker_index];
        worker.output.clear();
        try {
            for (const auto shard_index : worker.shard_indexes) {
                CollectActivePairsInShard(market, shards_[shard_index], worker.output);
            }
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            if (worker_exception_ == nullptr) {
                worker_exception_ = std::current_exception();
            }
        }

        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            ++completed_workers_;
        }
        worker_done_cv_.notify_one();
    }
}

void BasisArbitrageMultiPairRuntime::CollectActivePairsInShard(
    const MarketPtr& market,
    const PairShard& shard,
    std::vector<PairSignalSnapshot>& out)
{
    for (std::size_t i = shard.begin; i < shard.end; ++i) {
        auto signal = pair_runtime_states_[i].signal_engine.on_market(market);
        if (signal.status != QTrading::Signal::SignalStatus::Active) {
            continue;
        }
        out.push_back(PairSignalSnapshot{
            i,
            std::move(signal)
        });
    }
}

std::unordered_set<std::size_t> BasisArbitrageMultiPairRuntime::CollectExposedPairIndexes(
    const QTrading::Risk::AccountState& account) const
{
    std::unordered_set<std::size_t> out;
    auto maybe_add = [&](const std::string& symbol) {
        const auto it = symbol_to_pair_index_.find(symbol);
        if (it != symbol_to_pair_index_.end()) {
            out.insert(it->second);
        }
    };

    for (const auto& pos : account.positions) {
        maybe_add(pos.symbol);
    }
    for (const auto& ord : account.open_orders) {
        maybe_add(ord.symbol);
    }
    return out;
}

QTrading::Risk::RiskTarget BasisArbitrageMultiPairRuntime::ScaleRiskTarget(
    const QTrading::Risk::RiskTarget& input,
    double scale) const
{
    QTrading::Risk::RiskTarget out = input;
    for (auto& [symbol, target] : out.target_positions) {
        target *= scale;
    }
    out.risk_budget_used *= scale;
    return out;
}

void BasisArbitrageMultiPairRuntime::MergeRiskTarget(
    const QTrading::Risk::RiskTarget& input,
    QTrading::Risk::RiskTarget& merged) const
{
    merged.ts_ms = std::max(merged.ts_ms, input.ts_ms);
    if (merged.strategy.empty()) {
        merged.strategy = input.strategy;
    }
    merged.max_leverage = std::max(merged.max_leverage, input.max_leverage);
    merged.risk_budget_used += input.risk_budget_used;
    merged.block_new_entries = merged.block_new_entries || input.block_new_entries;
    merged.force_reduce = merged.force_reduce || input.force_reduce;

    for (const auto& [symbol, target] : input.target_positions) {
        merged.target_positions[symbol] = target;
    }
    for (const auto& [symbol, leverage] : input.leverage) {
        merged.leverage[symbol] = leverage;
    }
}

void BasisArbitrageMultiPairRuntime::RunOneCycle()
{
    auto market_opt = exchange_->get_market_channel()->Receive();
    if (!market_opt.has_value()) {
        return;
    }
    const auto& market = market_opt.value();
    InitializePairsIfNeeded(market);

    (void)universe_selector_.select();
    const auto account = exchange_gateway_.BuildAccountState();
    const auto exposed_pair_indexes = CollectExposedPairIndexes(account);
    const auto ranked_pairs = BuildActivePairRanking(market);

    for (const auto pair_index : active_pair_slot_touched_) {
        active_pair_slots_[pair_index] = -1;
    }
    active_pair_slot_touched_.clear();

    std::unordered_set<std::size_t> chosen_pair_indexes = exposed_pair_indexes;
    std::vector<PairSignalSnapshot> chosen_active_pairs;
    chosen_active_pairs.reserve(
        std::min<std::size_t>(runtime_cfg_.basis_multi_top_n, ranked_pairs.size()));

    for (const auto& ranked : ranked_pairs) {
        if (chosen_active_pairs.size() >= runtime_cfg_.basis_multi_top_n) {
            break;
        }
        chosen_pair_indexes.insert(ranked.pair_index);
        active_pair_slots_[ranked.pair_index] = static_cast<int>(chosen_active_pairs.size());
        active_pair_slot_touched_.push_back(ranked.pair_index);
        chosen_active_pairs.push_back(ranked);
    }

    QTrading::Risk::RiskTarget merged_risk{};
    merged_risk.strategy = "basis_arbitrage";
    QTrading::Signal::SignalDecision execution_signal_src =
        MakeInactiveBasisSignal(market ? market->Timestamp : 0);

    const double allocation_scale =
        chosen_active_pairs.empty() ? 0.0 : 1.0;
    double total_allocator_score = 0.0;
    for (const auto& pair : chosen_active_pairs) {
        total_allocator_score += std::max(pair.signal.allocator_score, kAllocatorScoreFloor);
    }

    for (const auto pair_index : chosen_pair_indexes) {
        auto& pair_runtime = pair_runtime_states_[pair_index];
        const auto& pair_static = pair_static_infos_[pair_index];
        const int active_slot = active_pair_slots_[pair_index];

        if (active_slot >= 0) {
            auto signal = chosen_active_pairs[static_cast<std::size_t>(active_slot)].signal;
            auto intent = pair_runtime.intent_builder.build(signal, market);
            auto risk = risk_engine_.position(intent, account, market);
            if (allocation_scale > 0.0 && total_allocator_score > 0.0) {
                const double pair_weight =
                    std::max(signal.allocator_score, kAllocatorScoreFloor) / total_allocator_score;
                risk = ScaleRiskTarget(risk, pair_weight);
            }
            MergeRiskTarget(risk, merged_risk);
            if (execution_signal_src.status != QTrading::Signal::SignalStatus::Active ||
                signal.confidence > execution_signal_src.confidence)
            {
                execution_signal_src = signal;
            }
            continue;
        }

        // Keep per-pair flatten explicit. Calling risk_engine_ with empty intent would
        // flatten the entire portfolio, which is not valid in multi-pair mode.
        QTrading::Risk::RiskTarget flatten_risk{};
        flatten_risk.ts_ms = market ? market->Timestamp : 0;
        flatten_risk.strategy = "basis_arbitrage";
        flatten_risk.target_positions[pair_static.spot_symbol] = 0.0;
        flatten_risk.target_positions[pair_static.perp_symbol] = 0.0;
        MergeRiskTarget(flatten_risk, merged_risk);
    }

    for (const auto pair_index : active_pair_slot_touched_) {
        active_pair_slots_[pair_index] = -1;
    }
    active_pair_slot_touched_.clear();

    const auto execution_signal = ToExecutionSignal(execution_signal_src);
    const auto orders = execution_orchestrator_.Execute(merged_risk, account, execution_signal, market);
    exchange_gateway_.SubmitOrders(orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Strategy
