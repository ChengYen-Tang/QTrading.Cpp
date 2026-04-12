#include "Strategy/BasisArbitrageMultiPairRuntime.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Signal/SignalDecision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace {

constexpr double kAllocatorScoreFloor = 1e-12;
constexpr double kExposureEpsilon = 1e-6;

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double ClampNonNegativeFinite(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

double SafeRatio(double numerator, double denominator)
{
    if (!(std::isfinite(numerator) && std::isfinite(denominator)) || denominator <= 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

double RampUp(double value, double threshold)
{
    if (!(std::isfinite(value) && std::isfinite(threshold)) || threshold <= 0.0) {
        return (value > 0.0) ? 1.0 : 0.0;
    }
    return Clamp01(value / threshold);
}

double RampDown(double value, double cap)
{
    if (!(std::isfinite(value) && std::isfinite(cap)) || cap <= 0.0) {
        return 1.0;
    }
    return Clamp01(1.0 - SafeRatio(value, cap));
}

double SignedOrderDeltaNotional(
    const QTrading::Execution::ExecutionOrder& order,
    double reference_price)
{
    if (!(reference_price > 0.0) || !(order.qty > 0.0)) {
        return 0.0;
    }
    const double sign =
        (order.action == QTrading::Execution::OrderAction::Buy) ? 1.0 : -1.0;
    return sign * order.qty * reference_price;
}

bool HasMaterialExposure(double signed_notional)
{
    return std::abs(signed_notional) > kExposureEpsilon;
}

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
    , allowed_raw_symbols_(runtime_cfg_.basis_allowed_raw_symbols)
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
    runtime_cfg_.basis_multi_min_score_ratio = Clamp01(runtime_cfg_.basis_multi_min_score_ratio);
    if (!std::isfinite(runtime_cfg_.basis_multi_confidence_power) ||
        runtime_cfg_.basis_multi_confidence_power < 0.0)
    {
        runtime_cfg_.basis_multi_confidence_power = 1.0;
    }
    runtime_cfg_.basis_multi_max_pair_weight =
        std::clamp(runtime_cfg_.basis_multi_max_pair_weight, 0.0, 1.0);
    runtime_cfg_.basis_multi_min_effective_quality_scale =
        Clamp01(runtime_cfg_.basis_multi_min_effective_quality_scale);
    if (!std::isfinite(runtime_cfg_.basis_multi_min_effective_allocator_score) ||
        runtime_cfg_.basis_multi_min_effective_allocator_score < 0.0) {
        runtime_cfg_.basis_multi_min_effective_allocator_score = 0.0;
    }
    if (!std::isfinite(runtime_cfg_.basis_pair_min_spot_quote_volume) ||
        runtime_cfg_.basis_pair_min_spot_quote_volume < 0.0) {
        runtime_cfg_.basis_pair_min_spot_quote_volume = 0.0;
    }
    if (!std::isfinite(runtime_cfg_.basis_pair_min_perp_quote_volume) ||
        runtime_cfg_.basis_pair_min_perp_quote_volume < 0.0) {
        runtime_cfg_.basis_pair_min_perp_quote_volume = 0.0;
    }
    if (!std::isfinite(runtime_cfg_.basis_pair_min_quote_volume_ratio) ||
        runtime_cfg_.basis_pair_min_quote_volume_ratio < 0.0) {
        runtime_cfg_.basis_pair_min_quote_volume_ratio = 0.0;
    }
    runtime_cfg_.basis_quality_window_bars =
        std::max<std::size_t>(runtime_cfg_.basis_quality_window_bars, 10);
    runtime_cfg_.basis_quality_min_samples =
        std::max<std::size_t>(runtime_cfg_.basis_quality_min_samples, 1);
    runtime_cfg_.basis_quality_min_samples =
        std::min(runtime_cfg_.basis_quality_min_samples, runtime_cfg_.basis_quality_window_bars);
    if (!std::isfinite(runtime_cfg_.basis_quality_min_abs_basis_p95_pct) ||
        runtime_cfg_.basis_quality_min_abs_basis_p95_pct < 0.0) {
        runtime_cfg_.basis_quality_min_abs_basis_p95_pct = 0.0;
    }
    runtime_cfg_.basis_quality_max_spot_zero_volume_share =
        Clamp01(runtime_cfg_.basis_quality_max_spot_zero_volume_share);
    if (!std::isfinite(runtime_cfg_.basis_quality_min_spot_perp_quote_ratio) ||
        runtime_cfg_.basis_quality_min_spot_perp_quote_ratio < 0.0) {
        runtime_cfg_.basis_quality_min_spot_perp_quote_ratio = 0.0;
    }
    runtime_cfg_.basis_quality_structural_min_samples =
        std::max<std::size_t>(runtime_cfg_.basis_quality_structural_min_samples, 0);
    if (!std::isfinite(runtime_cfg_.basis_quality_structural_min_abs_basis_mean_pct) ||
        runtime_cfg_.basis_quality_structural_min_abs_basis_mean_pct < 0.0) {
        runtime_cfg_.basis_quality_structural_min_abs_basis_mean_pct = 0.0;
    }
    runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share =
        Clamp01(runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share);
    if (!std::isfinite(runtime_cfg_.basis_quality_structural_max_spot_perp_quote_ratio) ||
        runtime_cfg_.basis_quality_structural_max_spot_perp_quote_ratio < 0.0) {
        runtime_cfg_.basis_quality_structural_max_spot_perp_quote_ratio = 0.0;
    }
    runtime_cfg_.basis_quality_structural_exception_max_spot_zero_volume_share =
        Clamp01(runtime_cfg_.basis_quality_structural_exception_max_spot_zero_volume_share);
    if (!std::isfinite(runtime_cfg_.basis_quality_structural_exception_min_abs_basis_mean_pct) ||
        runtime_cfg_.basis_quality_structural_exception_min_abs_basis_mean_pct < 0.0) {
        runtime_cfg_.basis_quality_structural_exception_min_abs_basis_mean_pct = 0.0;
    }
    if (!std::isfinite(runtime_cfg_.basis_quality_structural_exception_max_spot_perp_quote_ratio) ||
        runtime_cfg_.basis_quality_structural_exception_max_spot_perp_quote_ratio < 0.0) {
        runtime_cfg_.basis_quality_structural_exception_max_spot_perp_quote_ratio = 0.0;
    }
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
        if (!allowed_raw_symbols_.empty() &&
            allowed_raw_symbols_.find(raw_symbol) == allowed_raw_symbols_.end()) {
            continue;
        }
        const auto perp_symbol = raw_symbol + "_PERP";
        const auto perp_it = symbol_to_market_id.find(perp_symbol);
        if (perp_it == symbol_to_market_id.end()) {
            continue;
        }
        const std::size_t pair_index = pair_runtime_states_.size();
        pair_static_infos_.push_back(PairStaticInfo{
            raw_symbol,
            symbol,
            perp_symbol,
            i,
            perp_it->second
        });
        pair_runtime_states_.emplace_back(
            symbol,
            perp_symbol,
            base_signal_cfg_,
            base_intent_cfg_);
        if (runtime_cfg_.basis_quality_enabled) {
            auto& quality = pair_runtime_states_.back().quality_state;
            const std::size_t window_size =
                std::max<std::size_t>(runtime_cfg_.basis_quality_window_bars, 1);
            quality.abs_basis_window.assign(window_size, 0.0);
            quality.abs_basis_scratch.assign(window_size, 0.0);
            quality.quote_ratio_window.assign(window_size, 0.0);
            quality.spot_zero_window.assign(window_size, 0);
        }
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
    ApplyPortfolioAllocator(ranking_buffer_);
    return ranking_buffer_;
}

void BasisArbitrageMultiPairRuntime::ApplyPortfolioAllocator(
    std::vector<PairSignalSnapshot>& ranked_pairs) const
{
    if (ranked_pairs.empty()) {
        return;
    }

    const double best_score =
        std::max(ranked_pairs.front().signal.allocator_score, kAllocatorScoreFloor);
    const double min_score =
        std::max(kAllocatorScoreFloor, best_score * runtime_cfg_.basis_multi_min_score_ratio);
    const double confidence_power = runtime_cfg_.basis_multi_confidence_power;
    const double max_pair_weight = runtime_cfg_.basis_multi_max_pair_weight;

    double total_raw_weight = 0.0;
    std::size_t kept_count = 0;
    for (auto& pair : ranked_pairs) {
        const double score = std::max(pair.signal.allocator_score, 0.0);
        if (score < min_score) {
            pair.portfolio_weight = 0.0;
            continue;
        }

        const double confidence_scale =
            std::pow(Clamp01(pair.signal.confidence), confidence_power);
        const double raw_weight =
            std::max(score * std::max(confidence_scale, kAllocatorScoreFloor), kAllocatorScoreFloor);
        pair.portfolio_weight = raw_weight;
        total_raw_weight += raw_weight;
        ++kept_count;
    }

    if (kept_count == 0 || total_raw_weight <= 0.0) {
        ranked_pairs.clear();
        return;
    }

    for (auto& pair : ranked_pairs) {
        if (pair.portfolio_weight > 0.0) {
            pair.portfolio_weight /= total_raw_weight;
        }
    }

    if (max_pair_weight <= 0.0 || max_pair_weight >= 1.0 || ranked_pairs.size() <= 1) {
        ranked_pairs.erase(
            std::remove_if(
                ranked_pairs.begin(),
                ranked_pairs.end(),
                [](const PairSignalSnapshot& pair) { return pair.portfolio_weight <= 0.0; }),
            ranked_pairs.end());
        return;
    }

    double capped_total = 0.0;
    double residual_total = 0.0;
    for (auto& pair : ranked_pairs) {
        if (pair.portfolio_weight <= 0.0) {
            continue;
        }
        if (pair.portfolio_weight > max_pair_weight) {
            pair.portfolio_weight = max_pair_weight;
        }
        else {
            residual_total += pair.portfolio_weight;
        }
        capped_total += pair.portfolio_weight;
    }

    double remaining_capacity = std::max(0.0, 1.0 - capped_total);
    if (remaining_capacity > 0.0 && residual_total > 0.0) {
        for (auto& pair : ranked_pairs) {
            if (pair.portfolio_weight <= 0.0 || pair.portfolio_weight >= max_pair_weight) {
                continue;
            }
            pair.portfolio_weight += remaining_capacity * (pair.portfolio_weight / residual_total);
        }
    }

    double final_total = 0.0;
    for (const auto& pair : ranked_pairs) {
        final_total += pair.portfolio_weight;
    }
    if (final_total > 0.0) {
        for (auto& pair : ranked_pairs) {
            pair.portfolio_weight /= final_total;
        }
    }

    ranked_pairs.erase(
        std::remove_if(
            ranked_pairs.begin(),
            ranked_pairs.end(),
            [](const PairSignalSnapshot& pair) { return pair.portfolio_weight <= 0.0; }),
        ranked_pairs.end());
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
        if (runtime_cfg_.basis_quality_enabled) {
            UpdatePairQualityState(i, market);
        }

        auto signal = pair_runtime_states_[i].signal_engine.on_market(market);
        if (signal.status != QTrading::Signal::SignalStatus::Active) {
            continue;
        }

        const double quality_scale =
            runtime_cfg_.basis_quality_enabled ? ComputePairQualityScale(i) : 1.0;
        if (!(quality_scale > 0.0)) {
            continue;
        }

        const double liquidity_scale = ComputePairLiquidityScale(i, market);
        if (!(liquidity_scale > 0.0)) {
            continue;
        }

        const double effective_quality_scale = std::sqrt(quality_scale * liquidity_scale);
        if (effective_quality_scale < runtime_cfg_.basis_multi_min_effective_quality_scale) {
            continue;
        }

        const double total_scale = std::sqrt(quality_scale * liquidity_scale);
        signal.confidence = Clamp01(signal.confidence * total_scale);
        signal.allocator_score =
            std::max(0.0, signal.allocator_score * quality_scale * liquidity_scale);
        if (signal.allocator_score < runtime_cfg_.basis_multi_min_effective_allocator_score) {
            continue;
        }
        if (!(signal.confidence > 0.0) || !(signal.allocator_score > 0.0)) {
            continue;
        }

        out.push_back(PairSignalSnapshot{
            i,
            std::move(signal),
            0.0,
            quality_scale,
            liquidity_scale
        });
    }
}

void BasisArbitrageMultiPairRuntime::UpdatePairQualityState(
    std::size_t pair_index,
    const MarketPtr& market)
{
    if (!runtime_cfg_.basis_quality_enabled ||
        !market ||
        pair_index >= pair_static_infos_.size() ||
        pair_index >= pair_runtime_states_.size()) {
        return;
    }

    const auto& pair = pair_static_infos_[pair_index];
    auto& quality = pair_runtime_states_[pair_index].quality_state;
    if (quality.abs_basis_window.empty()) {
        const std::size_t window_size =
            std::max<std::size_t>(runtime_cfg_.basis_quality_window_bars, 1);
        quality.abs_basis_window.assign(window_size, 0.0);
        quality.abs_basis_scratch.assign(window_size, 0.0);
        quality.quote_ratio_window.assign(window_size, 0.0);
        quality.spot_zero_window.assign(window_size, 0);
        quality.next_index = 0;
        quality.sample_count = 0;
        quality.spot_zero_count = 0;
        quality.quote_ratio_sum = 0.0;
        quality.structural_sample_count = 0;
        quality.structural_spot_zero_count = 0;
        quality.structural_abs_basis_sum = 0.0;
        quality.structural_quote_ratio_sum = 0.0;
    }

    bool spot_zero_volume = true;
    double quote_ratio = 0.0;
    double abs_basis_pct = 0.0;

    if (pair.spot_market_id < market->trade_klines_by_id.size() &&
        pair.perp_market_id < market->trade_klines_by_id.size()) {
        const auto& spot = market->trade_klines_by_id[pair.spot_market_id];
        const auto& perp = market->trade_klines_by_id[pair.perp_market_id];
        if (spot.has_value() && perp.has_value() &&
            spot->ClosePrice > 0.0 && perp->ClosePrice > 0.0) {
            const double spot_quote_volume = ClampNonNegativeFinite(spot->QuoteVolume);
            const double perp_quote_volume = ClampNonNegativeFinite(perp->QuoteVolume);
            spot_zero_volume = !(spot_quote_volume > 0.0);
            if (perp_quote_volume > 0.0) {
                quote_ratio = spot_quote_volume / perp_quote_volume;
            }
            abs_basis_pct = std::fabs((perp->ClosePrice - spot->ClosePrice) / spot->ClosePrice);
        }
    }

    const std::size_t capacity = quality.abs_basis_window.size();
    if (capacity == 0) {
        return;
    }

    const std::size_t slot = quality.next_index;
    if (quality.sample_count == capacity) {
        quality.spot_zero_count -= static_cast<std::size_t>(quality.spot_zero_window[slot]);
        quality.quote_ratio_sum -= quality.quote_ratio_window[slot];
    }
    else {
        ++quality.sample_count;
    }

    quality.abs_basis_window[slot] = ClampNonNegativeFinite(abs_basis_pct);
    quality.quote_ratio_window[slot] = ClampNonNegativeFinite(quote_ratio);
    quality.spot_zero_window[slot] = spot_zero_volume ? 1 : 0;
    quality.spot_zero_count += static_cast<std::size_t>(quality.spot_zero_window[slot]);
    quality.quote_ratio_sum += quality.quote_ratio_window[slot];
    quality.next_index = (slot + 1) % capacity;
    ++quality.structural_sample_count;
    quality.structural_spot_zero_count += static_cast<std::size_t>(spot_zero_volume ? 1 : 0);
    quality.structural_abs_basis_sum += ClampNonNegativeFinite(abs_basis_pct);
    quality.structural_quote_ratio_sum += ClampNonNegativeFinite(quote_ratio);
}

double BasisArbitrageMultiPairRuntime::ComputePairQualityScale(std::size_t pair_index)
{
    if (!runtime_cfg_.basis_quality_enabled ||
        pair_index >= pair_runtime_states_.size()) {
        return 1.0;
    }

    auto& quality = pair_runtime_states_[pair_index].quality_state;
    if (quality.sample_count < runtime_cfg_.basis_quality_min_samples) {
        return 0.0;
    }

    if (quality.abs_basis_scratch.size() < quality.sample_count) {
        return 0.0;
    }
    std::copy_n(
        quality.abs_basis_window.begin(),
        static_cast<std::ptrdiff_t>(quality.sample_count),
        quality.abs_basis_scratch.begin());
    const std::size_t p95_index =
        static_cast<std::size_t>(std::ceil(static_cast<double>(quality.sample_count) * 0.95)) - 1;
    auto nth = quality.abs_basis_scratch.begin() + static_cast<std::ptrdiff_t>(p95_index);
    std::nth_element(
        quality.abs_basis_scratch.begin(),
        nth,
        quality.abs_basis_scratch.begin() + static_cast<std::ptrdiff_t>(quality.sample_count));
    const double abs_basis_p95 = *nth;
    const double zero_volume_share =
        static_cast<double>(quality.spot_zero_count) /
        static_cast<double>(std::max<std::size_t>(quality.sample_count, 1));
    const double avg_quote_ratio =
        quality.quote_ratio_sum / static_cast<double>(std::max<std::size_t>(quality.sample_count, 1));
    const double structural_abs_basis_mean =
        quality.structural_abs_basis_sum /
        static_cast<double>(std::max<std::size_t>(quality.structural_sample_count, 1));
    const double structural_zero_volume_share =
        static_cast<double>(quality.structural_spot_zero_count) /
        static_cast<double>(std::max<std::size_t>(quality.structural_sample_count, 1));
    const double structural_avg_quote_ratio =
        quality.structural_quote_ratio_sum /
        static_cast<double>(std::max<std::size_t>(quality.structural_sample_count, 1));

    if (runtime_cfg_.basis_quality_structural_min_samples > 0 &&
        quality.structural_sample_count < runtime_cfg_.basis_quality_structural_min_samples) {
        return 0.0;
    }

    const double local_basis_scale =
        RampUp(abs_basis_p95, runtime_cfg_.basis_quality_min_abs_basis_p95_pct);
    const double local_zero_scale =
        RampDown(zero_volume_share, runtime_cfg_.basis_quality_max_spot_zero_volume_share);
    const double local_quote_scale =
        RampUp(avg_quote_ratio, runtime_cfg_.basis_quality_min_spot_perp_quote_ratio);
    const double local_scale =
        local_basis_scale * local_zero_scale * local_quote_scale;

    const double structural_basis_scale =
        RampUp(
            structural_abs_basis_mean,
            runtime_cfg_.basis_quality_structural_min_abs_basis_mean_pct);
    const double structural_zero_scale =
        RampDown(
            structural_zero_volume_share,
            runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share);
    double structural_quote_scale = 1.0;
    if (runtime_cfg_.basis_quality_structural_max_spot_perp_quote_ratio > 0.0) {
        structural_quote_scale =
            RampDown(
                structural_avg_quote_ratio,
                runtime_cfg_.basis_quality_structural_max_spot_perp_quote_ratio);
    }
    const double structural_base_scale =
        structural_basis_scale * structural_zero_scale * structural_quote_scale;

    double structural_exception_scale = 0.0;
    if (structural_zero_volume_share >
        runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share)
    {
        const double exception_zero_denominator =
            std::max(
                runtime_cfg_.basis_quality_structural_exception_max_spot_zero_volume_share -
                    runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share,
                1e-12);
        const double exception_zero_scale = Clamp01(
            1.0 -
            (structural_zero_volume_share -
                runtime_cfg_.basis_quality_structural_max_spot_zero_volume_share) /
                exception_zero_denominator);
        const double exception_basis_scale =
            RampUp(
                structural_abs_basis_mean,
                runtime_cfg_.basis_quality_structural_exception_min_abs_basis_mean_pct);
        double exception_quote_scale = 1.0;
        if (runtime_cfg_.basis_quality_structural_exception_max_spot_perp_quote_ratio > 0.0) {
            exception_quote_scale =
                RampDown(
                    structural_avg_quote_ratio,
                    runtime_cfg_.basis_quality_structural_exception_max_spot_perp_quote_ratio);
        }
        structural_exception_scale =
            exception_zero_scale * exception_basis_scale * exception_quote_scale;
    }

    const double structural_scale =
        std::max(structural_base_scale, structural_exception_scale);
    return Clamp01(std::sqrt(local_scale * structural_scale));
}

double BasisArbitrageMultiPairRuntime::ComputePairLiquidityScale(
    std::size_t pair_index,
    const MarketPtr& market) const
{
    if (!market || pair_index >= pair_static_infos_.size()) {
        return 0.0;
    }

    const auto& pair = pair_static_infos_[pair_index];
    if (pair.spot_market_id >= market->trade_klines_by_id.size() ||
        pair.perp_market_id >= market->trade_klines_by_id.size()) {
        return 0.0;
    }

    const auto& spot = market->trade_klines_by_id[pair.spot_market_id];
    const auto& perp = market->trade_klines_by_id[pair.perp_market_id];
    if (!spot.has_value() || !perp.has_value()) {
        return 0.0;
    }
    if (!(spot->ClosePrice > 0.0) || !(perp->ClosePrice > 0.0)) {
        return 0.0;
    }

    const double spot_quote_volume = std::max(0.0, spot->QuoteVolume);
    const double perp_quote_volume = std::max(0.0, perp->QuoteVolume);
    const double spot_scale =
        RampUp(spot_quote_volume, runtime_cfg_.basis_pair_min_spot_quote_volume);
    const double perp_scale =
        RampUp(perp_quote_volume, runtime_cfg_.basis_pair_min_perp_quote_volume);

    double ratio_scale = 1.0;
    if (runtime_cfg_.basis_pair_min_quote_volume_ratio > 0.0) {
        if (!(perp_quote_volume > 0.0)) {
            return 0.0;
        }
        const double ratio = spot_quote_volume / perp_quote_volume;
        ratio_scale =
            RampUp(ratio, runtime_cfg_.basis_pair_min_quote_volume_ratio);
    }

    return Clamp01(std::sqrt(spot_scale * perp_scale) * ratio_scale);
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

void BasisArbitrageMultiPairRuntime::NormalizePairTargetsToAvoidSingleLegExposure(
    QTrading::Risk::RiskTarget& target,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    if (!market || !market->symbols || pair_static_infos_.empty()) {
        return;
    }

    std::unordered_map<std::string, double> reference_price_by_symbol;
    reference_price_by_symbol.reserve(market->symbols->size());
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i >= market->trade_klines_by_id.size() || !market->trade_klines_by_id[i].has_value()) {
            continue;
        }
        const double price = market->trade_klines_by_id[i]->ClosePrice;
        if (price > 0.0) {
            reference_price_by_symbol.emplace(symbols[i], price);
        }
    }

    struct PairNotionalState {
        double current_spot = 0.0;
        double current_perp = 0.0;
    };
    std::vector<PairNotionalState> pair_states(pair_static_infos_.size());

    for (const auto& position : account.positions) {
        const auto pair_it = symbol_to_pair_index_.find(position.symbol);
        if (pair_it == symbol_to_pair_index_.end()) {
            continue;
        }
        const auto price_it = reference_price_by_symbol.find(position.symbol);
        if (price_it == reference_price_by_symbol.end() || !(price_it->second > 0.0)) {
            continue;
        }
        const double signed_notional =
            (position.is_long ? 1.0 : -1.0) * position.quantity * price_it->second;
        auto& state = pair_states[pair_it->second];
        if (position.symbol == pair_static_infos_[pair_it->second].spot_symbol) {
            state.current_spot += signed_notional;
        }
        else if (position.symbol == pair_static_infos_[pair_it->second].perp_symbol) {
            state.current_perp += signed_notional;
        }
    }

    for (std::size_t pair_index = 0; pair_index < pair_static_infos_.size(); ++pair_index) {
        const auto& pair = pair_static_infos_[pair_index];
        const auto& state = pair_states[pair_index];
        const double current_spot = state.current_spot;
        const double current_perp = state.current_perp;
        const auto spot_it = target.target_positions.find(pair.spot_symbol);
        const auto perp_it = target.target_positions.find(pair.perp_symbol);
        const double target_spot = (spot_it == target.target_positions.end()) ? current_spot : spot_it->second;
        const double target_perp = (perp_it == target.target_positions.end()) ? current_perp : perp_it->second;

        const int current_open_legs =
            static_cast<int>(HasMaterialExposure(current_spot)) +
            static_cast<int>(HasMaterialExposure(current_perp));
        const int target_open_legs =
            static_cast<int>(HasMaterialExposure(target_spot)) +
            static_cast<int>(HasMaterialExposure(target_perp));

        bool clamp_to_current = false;
        if (current_open_legs == 0) {
            clamp_to_current = target_open_legs == 1;
        }
        else if (current_open_legs == 1) {
            if (target_open_legs == 2 || target_open_legs == 0) {
                clamp_to_current = false;
            }
            else {
                const bool current_has_spot = HasMaterialExposure(current_spot);
                const double current_single =
                    current_has_spot ? std::abs(current_spot) : std::abs(current_perp);
                const double target_single =
                    current_has_spot ? std::abs(target_spot) : std::abs(target_perp);
                clamp_to_current = target_single > current_single + kExposureEpsilon;
            }
        }
        else {
            clamp_to_current = target_open_legs == 1;
        }

        if (!clamp_to_current) {
            continue;
        }

        target.target_positions[pair.spot_symbol] = current_spot;
        target.target_positions[pair.perp_symbol] = current_perp;
    }
}

std::vector<QTrading::Execution::ExecutionOrder>
BasisArbitrageMultiPairRuntime::FilterOrdersToAvoidSingleLegExposure(
    const std::vector<QTrading::Execution::ExecutionOrder>& orders,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    if (orders.empty() || !market || !market->symbols || pair_static_infos_.empty()) {
        return orders;
    }

    std::unordered_map<std::string, double> reference_price_by_symbol;
    reference_price_by_symbol.reserve(market->symbols->size());
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i >= market->trade_klines_by_id.size() || !market->trade_klines_by_id[i].has_value()) {
            continue;
        }
        const double price = market->trade_klines_by_id[i]->ClosePrice;
        if (price > 0.0) {
            reference_price_by_symbol.emplace(symbols[i], price);
        }
    }

    struct PairOrderState {
        bool has_spot_order = false;
        bool has_perp_order = false;
        double current_spot_notional = 0.0;
        double current_perp_notional = 0.0;
        double projected_spot_notional = 0.0;
        double projected_perp_notional = 0.0;
        std::vector<std::size_t> order_indexes;
    };

    std::vector<PairOrderState> pair_order_states(pair_static_infos_.size());

    for (const auto& position : account.positions) {
        const auto pair_it = symbol_to_pair_index_.find(position.symbol);
        if (pair_it == symbol_to_pair_index_.end()) {
            continue;
        }
        const auto price_it = reference_price_by_symbol.find(position.symbol);
        if (price_it == reference_price_by_symbol.end() || !(price_it->second > 0.0)) {
            continue;
        }
        const double signed_notional =
            (position.is_long ? 1.0 : -1.0) * position.quantity * price_it->second;
        auto& state = pair_order_states[pair_it->second];
        if (position.symbol == pair_static_infos_[pair_it->second].spot_symbol) {
            state.current_spot_notional += signed_notional;
        }
        else if (position.symbol == pair_static_infos_[pair_it->second].perp_symbol) {
            state.current_perp_notional += signed_notional;
        }
    }

    for (std::size_t i = 0; i < orders.size(); ++i) {
        const auto& order = orders[i];
        const auto pair_it = symbol_to_pair_index_.find(order.symbol);
        if (pair_it == symbol_to_pair_index_.end()) {
            continue;
        }
        const auto price_it = reference_price_by_symbol.find(order.symbol);
        const double reference_price =
            (price_it != reference_price_by_symbol.end()) ? price_it->second : order.price;
        const double delta_notional = SignedOrderDeltaNotional(order, reference_price);
        auto& state = pair_order_states[pair_it->second];
        state.order_indexes.push_back(i);
        if (order.symbol == pair_static_infos_[pair_it->second].spot_symbol) {
            state.has_spot_order = true;
            state.projected_spot_notional += delta_notional;
        }
        else if (order.symbol == pair_static_infos_[pair_it->second].perp_symbol) {
            state.has_perp_order = true;
            state.projected_perp_notional += delta_notional;
        }
    }

    std::vector<bool> keep_order(orders.size(), true);
    for (std::size_t pair_index = 0; pair_index < pair_order_states.size(); ++pair_index) {
        const auto& state = pair_order_states[pair_index];
        if (state.order_indexes.empty()) {
            continue;
        }
        if (state.has_spot_order && state.has_perp_order) {
            continue;
        }

        const double current_spot = state.current_spot_notional;
        const double current_perp = state.current_perp_notional;
        const double projected_spot = current_spot + state.projected_spot_notional;
        const double projected_perp = current_perp + state.projected_perp_notional;

        const int current_open_legs =
            static_cast<int>(HasMaterialExposure(current_spot)) +
            static_cast<int>(HasMaterialExposure(current_perp));
        const int projected_open_legs =
            static_cast<int>(HasMaterialExposure(projected_spot)) +
            static_cast<int>(HasMaterialExposure(projected_perp));

        bool drop_pair_orders = false;
        if (current_open_legs == 0) {
            drop_pair_orders = projected_open_legs == 1;
        }
        else if (current_open_legs == 1) {
            if (projected_open_legs == 2 || projected_open_legs == 0) {
                drop_pair_orders = false;
            }
            else {
                const bool current_has_spot = HasMaterialExposure(current_spot);
                const double current_single =
                    current_has_spot ? std::abs(current_spot) : std::abs(current_perp);
                const double projected_single =
                    current_has_spot ? std::abs(projected_spot) : std::abs(projected_perp);
                drop_pair_orders = projected_single > current_single + kExposureEpsilon;
            }
        }
        else {
            drop_pair_orders = projected_open_legs == 1;
        }

        if (!drop_pair_orders) {
            continue;
        }

        for (const auto order_index : state.order_indexes) {
            keep_order[order_index] = false;
        }
    }

    std::vector<QTrading::Execution::ExecutionOrder> filtered;
    filtered.reserve(orders.size());
    for (std::size_t i = 0; i < orders.size(); ++i) {
        if (keep_order[i]) {
            filtered.push_back(orders[i]);
        }
    }
    return filtered;
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

    for (const auto pair_index : chosen_pair_indexes) {
        auto& pair_runtime = pair_runtime_states_[pair_index];
        const auto& pair_static = pair_static_infos_[pair_index];
        const int active_slot = active_pair_slots_[pair_index];

        if (active_slot >= 0) {
            const auto& chosen_pair = chosen_active_pairs[static_cast<std::size_t>(active_slot)];
            auto signal = chosen_pair.signal;
            auto intent = pair_runtime.intent_builder.build(signal, market);
            auto risk = risk_engine_.position(intent, account, market);
            if (chosen_pair.portfolio_weight > 0.0) {
                const double pair_weight = chosen_pair.portfolio_weight;
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
    NormalizePairTargetsToAvoidSingleLegExposure(merged_risk, account, market);
    const auto orders = execution_orchestrator_.Execute(merged_risk, account, execution_signal, market);
    const auto filtered_orders = FilterOrdersToAvoidSingleLegExposure(orders, account, market);
    exchange_gateway_.SubmitOrders(filtered_orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Strategy
