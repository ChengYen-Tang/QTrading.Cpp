#include "Strategy/BasisArbitrageMultiPairRuntime.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Signal/SignalDecision.hpp"

#include <algorithm>
#include <unordered_set>

namespace {

constexpr std::size_t kBasisTopN = 3;
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

BasisArbitrageMultiPairRuntime::PairContext::PairContext(
    std::string raw_symbol_in,
    std::string spot_symbol_in,
    std::string perp_symbol_in,
    QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
    QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg)
    : raw_symbol(std::move(raw_symbol_in))
    , spot_symbol(std::move(spot_symbol_in))
    , perp_symbol(std::move(perp_symbol_in))
    , signal_engine([&]() {
        signal_cfg.spot_symbol = spot_symbol;
        signal_cfg.perp_symbol = perp_symbol;
        return QTrading::Signal::BasisArbitrageSignalEngine(std::move(signal_cfg));
    }())
    , intent_builder([&]() {
        intent_cfg.spot_symbol = spot_symbol;
        intent_cfg.perp_symbol = perp_symbol;
        return QTrading::Intent::BasisArbitrageIntentBuilder(std::move(intent_cfg));
    }())
{
}

BasisArbitrageMultiPairRuntime::BasisArbitrageMultiPairRuntime(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    QTrading::Universe::IUniverseSelector& universe_selector,
    QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
    QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg,
    QTrading::Risk::SimpleRiskEngine& risk_engine,
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
    QTrading::Monitoring::SimpleMonitoring& monitoring,
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types)
    : exchange_(std::move(exchange))
    , universe_selector_(universe_selector)
    , base_signal_cfg_(std::move(signal_cfg))
    , base_intent_cfg_(std::move(intent_cfg))
    , risk_engine_(risk_engine)
    , execution_engine_(execution_engine)
    , monitoring_(monitoring)
    , execution_scheduler_()
    , execution_policy_()
    , execution_orchestrator_(execution_engine_, execution_scheduler_, execution_policy_)
    , exchange_gateway_(exchange_, std::move(instrument_types))
{
}

void BasisArbitrageMultiPairRuntime::InitializePairsIfNeeded(const MarketPtr& market)
{
    if (!pair_contexts_.empty() || !market || !market->symbols) {
        return;
    }

    const auto& symbols = *market->symbols;
    std::unordered_set<std::string> all_symbols(symbols.begin(), symbols.end());
    pair_contexts_.reserve(symbols.size() / 2);

    for (const auto& symbol : symbols) {
        if (!EndsWith(symbol, "_SPOT")) {
            continue;
        }
        const auto raw_symbol = StripSuffix(symbol, "_SPOT");
        const auto perp_symbol = raw_symbol + "_PERP";
        if (!all_symbols.contains(perp_symbol)) {
            continue;
        }
        pair_contexts_.emplace_back(
            raw_symbol,
            symbol,
            perp_symbol,
            base_signal_cfg_,
            base_intent_cfg_);
    }
}

bool BasisArbitrageMultiPairRuntime::AccountHasExposure(const QTrading::Risk::AccountState& account) const
{
    return !account.positions.empty() || !account.open_orders.empty();
}

std::vector<BasisArbitrageMultiPairRuntime::PairSignalSnapshot>
BasisArbitrageMultiPairRuntime::BuildActivePairRanking(const MarketPtr& market)
{
    std::vector<PairSignalSnapshot> ranked;
    ranked.reserve(pair_contexts_.size());
    for (std::size_t i = 0; i < pair_contexts_.size(); ++i) {
        auto signal = pair_contexts_[i].signal_engine.on_market(market);
        if (signal.status != QTrading::Signal::SignalStatus::Active) {
            continue;
        }
        ranked.push_back(PairSignalSnapshot{
            i,
            std::move(signal)
        });
    }

    std::sort(
        ranked.begin(),
        ranked.end(),
        [&](const PairSignalSnapshot& lhs, const PairSignalSnapshot& rhs) {
            if (lhs.signal.allocator_score != rhs.signal.allocator_score) {
                return lhs.signal.allocator_score > rhs.signal.allocator_score;
            }
            if (lhs.signal.confidence != rhs.signal.confidence) {
                return lhs.signal.confidence > rhs.signal.confidence;
            }
            return pair_contexts_[lhs.pair_index].raw_symbol < pair_contexts_[rhs.pair_index].raw_symbol;
        });
    return ranked;
}

std::unordered_set<std::size_t> BasisArbitrageMultiPairRuntime::CollectExposedPairIndexes(
    const QTrading::Risk::AccountState& account) const
{
    std::unordered_set<std::size_t> out;
    auto maybe_add = [&](const std::string& symbol) {
        for (std::size_t i = 0; i < pair_contexts_.size(); ++i) {
            const auto& pair = pair_contexts_[i];
            if (symbol == pair.spot_symbol || symbol == pair.perp_symbol) {
                out.insert(i);
                return;
            }
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

    std::unordered_set<std::size_t> chosen_pair_indexes = exposed_pair_indexes;
    std::vector<PairSignalSnapshot> chosen_active_pairs;
    chosen_active_pairs.reserve(std::min<std::size_t>(kBasisTopN, ranked_pairs.size()));

    for (const auto& ranked : ranked_pairs) {
        if (chosen_active_pairs.size() >= kBasisTopN) {
            break;
        }
        chosen_pair_indexes.insert(ranked.pair_index);
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
        auto& pair = pair_contexts_[pair_index];

        auto active_it = std::find_if(
            chosen_active_pairs.begin(),
            chosen_active_pairs.end(),
            [&](const PairSignalSnapshot& item) { return item.pair_index == pair_index; });

        if (active_it != chosen_active_pairs.end()) {
            auto signal = active_it->signal;
            auto intent = pair.intent_builder.build(signal, market);
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
        flatten_risk.target_positions[pair.spot_symbol] = 0.0;
        flatten_risk.target_positions[pair.perp_symbol] = 0.0;
        MergeRiskTarget(flatten_risk, merged_risk);
    }

    const auto execution_signal = ToExecutionSignal(execution_signal_src);
    const auto orders = execution_orchestrator_.Execute(merged_risk, account, execution_signal, market);
    exchange_gateway_.SubmitOrders(orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Strategy
