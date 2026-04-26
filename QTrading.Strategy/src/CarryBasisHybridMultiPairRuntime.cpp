#include "Strategy/CarryBasisHybridMultiPairRuntime.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace {

constexpr double kScoreFloor = 1e-12;

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

double Clamp01(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
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

QTrading::Signal::SignalDecision MakeInactiveHybridSignal(std::uint64_t ts_ms)
{
    QTrading::Signal::SignalDecision out{};
    out.ts_ms = ts_ms;
    out.strategy = "carry_basis_hybrid";
    out.strategy_kind = QTrading::Contracts::StrategyKind::FundingCarry;
    out.status = QTrading::Signal::SignalStatus::Inactive;
    out.confidence = 0.0;
    out.urgency = QTrading::Signal::SignalUrgency::Low;
    return out;
}

} // namespace

namespace QTrading::Strategy {

CarryBasisHybridMultiPairRuntime::PairRuntimeState::PairRuntimeState(
    std::string spot_symbol,
    std::string perp_symbol,
    QTrading::Signal::CarryBasisHybridSignalEngine::Config signal_cfg,
    QTrading::Intent::CarryBasisHybridIntentBuilder::Config intent_cfg)
    : signal_engine([&]() {
        signal_cfg.funding_cfg.spot_symbol = spot_symbol;
        signal_cfg.funding_cfg.perp_symbol = perp_symbol;
        signal_cfg.basis_cfg.spot_symbol = spot_symbol;
        signal_cfg.basis_cfg.perp_symbol = perp_symbol;
        return QTrading::Signal::CarryBasisHybridSignalEngine(std::move(signal_cfg));
    }())
    , intent_builder([&]() {
        intent_cfg.spot_symbol = spot_symbol;
        intent_cfg.perp_symbol = perp_symbol;
        return QTrading::Intent::CarryBasisHybridIntentBuilder(std::move(intent_cfg));
    }())
{
}

CarryBasisHybridMultiPairRuntime::CarryBasisHybridMultiPairRuntime(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    QTrading::Universe::IUniverseSelector& universe_selector,
    QTrading::Signal::CarryBasisHybridSignalEngine::Config signal_cfg,
    QTrading::Intent::CarryBasisHybridIntentBuilder::Config intent_cfg,
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
    runtime_cfg_.basis_multi_min_score_ratio = Clamp01(runtime_cfg_.basis_multi_min_score_ratio);
    runtime_cfg_.basis_multi_max_pair_weight =
        std::clamp(runtime_cfg_.basis_multi_max_pair_weight, 0.0, 1.0);
    runtime_cfg_.carry_basis_turnover_cost_rate =
        std::max(0.0, runtime_cfg_.carry_basis_turnover_cost_rate);
    runtime_cfg_.carry_basis_turnover_expected_funding_settlements =
        std::max(0.0, runtime_cfg_.carry_basis_turnover_expected_funding_settlements);
    runtime_cfg_.carry_basis_turnover_min_gain_to_cost =
        std::max(0.0, runtime_cfg_.carry_basis_turnover_min_gain_to_cost);
    runtime_cfg_.carry_basis_delta_max_base_imbalance_ratio =
        Clamp01(runtime_cfg_.carry_basis_delta_max_base_imbalance_ratio);
}

void CarryBasisHybridMultiPairRuntime::InitializePairsIfNeeded(const MarketPtr& market)
{
    if (!pair_static_infos_.empty() || !market || !market->symbols) {
        return;
    }

    const auto& symbols = *market->symbols;
    std::unordered_map<std::string, std::size_t> symbol_to_market_id;
    symbol_to_market_id.reserve(symbols.size());
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        symbol_to_market_id.emplace(symbols[i], i);
    }

    for (const auto& symbol : symbols) {
        if (!EndsWith(symbol, "_SPOT")) {
            continue;
        }
        const std::string raw = StripSuffix(symbol, "_SPOT");
        const std::string perp = raw + "_PERP";
        if (symbol_to_market_id.find(perp) == symbol_to_market_id.end()) {
            continue;
        }
        if (!runtime_cfg_.basis_allowed_raw_symbols.empty() &&
            runtime_cfg_.basis_allowed_raw_symbols.find(raw) == runtime_cfg_.basis_allowed_raw_symbols.end()) {
            continue;
        }

        const std::size_t pair_index = pair_static_infos_.size();
        pair_static_infos_.push_back(PairStaticInfo{ raw, symbol, perp });
        pair_runtime_states_.emplace_back(
            symbol,
            perp,
            base_signal_cfg_,
            base_intent_cfg_);
        symbol_to_pair_index_.emplace(symbol, pair_index);
        symbol_to_pair_index_.emplace(perp, pair_index);
    }
    ranking_buffer_.reserve(pair_runtime_states_.size());
}

std::vector<CarryBasisHybridMultiPairRuntime::PairSignalSnapshot>
CarryBasisHybridMultiPairRuntime::BuildActivePairRanking(const MarketPtr& market)
{
    ranking_buffer_.clear();
    for (std::size_t i = 0; i < pair_runtime_states_.size(); ++i) {
        auto signal = pair_runtime_states_[i].signal_engine.on_market(market);
        if (signal.status != QTrading::Signal::SignalStatus::Active ||
            !(signal.confidence > 0.0)) {
            continue;
        }
        if (!(signal.allocator_score > 0.0)) {
            signal.allocator_score = std::max(signal.confidence, kScoreFloor);
        }
        ranking_buffer_.push_back(PairSignalSnapshot{ i, std::move(signal), 0.0 });
    }

    const auto cmp = [&](const PairSignalSnapshot& lhs, const PairSignalSnapshot& rhs) {
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
            cmp);
        ranking_buffer_.resize(top_n);
    }
    std::sort(ranking_buffer_.begin(), ranking_buffer_.end(), cmp);

    double total_weight = 0.0;
    const double best_score = std::max(ranking_buffer_.front().signal.allocator_score, kScoreFloor);
    const double min_score = std::max(kScoreFloor, best_score * runtime_cfg_.basis_multi_min_score_ratio);
    for (auto& item : ranking_buffer_) {
        if (item.signal.allocator_score < min_score) {
            item.portfolio_weight = 0.0;
            continue;
        }
        item.portfolio_weight =
            std::max(item.signal.allocator_score, kScoreFloor) *
            std::max(item.signal.confidence, kScoreFloor);
        total_weight += item.portfolio_weight;
    }
    if (!(total_weight > 0.0)) {
        ranking_buffer_.clear();
        return ranking_buffer_;
    }
    for (auto& item : ranking_buffer_) {
        item.portfolio_weight /= total_weight;
        if (runtime_cfg_.basis_multi_max_pair_weight > 0.0 &&
            runtime_cfg_.basis_multi_max_pair_weight < 1.0) {
            item.portfolio_weight =
                std::min(item.portfolio_weight, runtime_cfg_.basis_multi_max_pair_weight);
        }
    }
    if (runtime_cfg_.basis_multi_max_pair_weight > 0.0 &&
        runtime_cfg_.basis_multi_max_pair_weight < 1.0)
    {
        for (std::size_t pass = 0; pass < ranking_buffer_.size(); ++pass) {
            double used = 0.0;
            double expandable = 0.0;
            for (const auto& item : ranking_buffer_) {
                used += item.portfolio_weight;
                if (item.portfolio_weight < runtime_cfg_.basis_multi_max_pair_weight) {
                    expandable += item.portfolio_weight;
                }
            }
            const double remaining = 1.0 - used;
            if (remaining <= kScoreFloor || expandable <= kScoreFloor) {
                break;
            }
            bool changed = false;
            for (auto& item : ranking_buffer_) {
                if (item.portfolio_weight >= runtime_cfg_.basis_multi_max_pair_weight) {
                    continue;
                }
                const double add = remaining * (item.portfolio_weight / expandable);
                const double next = std::min(
                    runtime_cfg_.basis_multi_max_pair_weight,
                    item.portfolio_weight + add);
                changed = changed || (next > item.portfolio_weight + kScoreFloor);
                item.portfolio_weight = next;
            }
            if (!changed) {
                break;
            }
        }
    }
    return ranking_buffer_;
}

std::unordered_set<std::size_t> CarryBasisHybridMultiPairRuntime::CollectExposedPairIndexes(
    const QTrading::Risk::AccountState& account) const
{
    std::unordered_set<std::size_t> out;
    for (const auto& pos : account.positions) {
        if (!(pos.quantity > 0.0)) {
            continue;
        }
        const auto it = symbol_to_pair_index_.find(pos.symbol);
        if (it != symbol_to_pair_index_.end()) {
            out.insert(it->second);
        }
    }
    for (const auto& order : account.open_orders) {
        if (!(order.quantity > 0.0)) {
            continue;
        }
        const auto it = symbol_to_pair_index_.find(order.symbol);
        if (it != symbol_to_pair_index_.end()) {
            out.insert(it->second);
        }
    }
    return out;
}

QTrading::Risk::RiskTarget CarryBasisHybridMultiPairRuntime::ScaleRiskTarget(
    const QTrading::Risk::RiskTarget& input,
    double scale) const
{
    QTrading::Risk::RiskTarget out = input;
    for (auto& kv : out.target_positions) {
        kv.second *= scale;
    }
    out.risk_budget_used *= scale;
    return out;
}

void CarryBasisHybridMultiPairRuntime::MergeRiskTarget(
    const QTrading::Risk::RiskTarget& input,
    QTrading::Risk::RiskTarget& merged) const
{
    merged.ts_ms = std::max(merged.ts_ms, input.ts_ms);
    if (merged.strategy.empty()) {
        merged.strategy = "carry_basis_hybrid";
    }
    merged.max_leverage = std::max(merged.max_leverage, input.max_leverage);
    merged.risk_budget_used += input.risk_budget_used;
    for (const auto& kv : input.target_positions) {
        merged.target_positions[kv.first] = kv.second;
    }
    for (const auto& kv : input.leverage) {
        merged.leverage[kv.first] = kv.second;
    }
}

double CarryBasisHybridMultiPairRuntime::LastPrice(
    const std::string& symbol,
    const MarketPtr& market) const
{
    if (!market || !market->symbols) {
        return 0.0;
    }
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] != symbol) {
            continue;
        }
        if (i >= market->trade_klines_by_id.size() ||
            !market->trade_klines_by_id[i].has_value())
        {
            return 0.0;
        }
        const double price = market->trade_klines_by_id[i]->ClosePrice;
        return (std::isfinite(price) && price > 0.0) ? price : 0.0;
    }
    return 0.0;
}

double CarryBasisHybridMultiPairRuntime::LatestFundingRate(
    const std::string& symbol,
    const MarketPtr& market) const
{
    if (!market || !market->symbols) {
        return 0.0;
    }
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] != symbol) {
            continue;
        }
        if (i >= market->funding_by_id.size() ||
            !market->funding_by_id[i].has_value())
        {
            return 0.0;
        }
        const double rate = market->funding_by_id[i]->Rate;
        return std::isfinite(rate) ? rate : 0.0;
    }
    return 0.0;
}

double CarryBasisHybridMultiPairRuntime::CurrentSignedNotional(
    const std::string& symbol,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    const double price = LastPrice(symbol, market);
    if (!(price > 0.0)) {
        return 0.0;
    }

    double notional = 0.0;
    for (const auto& pos : account.positions) {
        if (pos.symbol != symbol || !(pos.quantity > 0.0)) {
            continue;
        }
        notional += pos.quantity * price * (pos.is_long ? 1.0 : -1.0);
    }
    return notional;
}

double CarryBasisHybridMultiPairRuntime::CurrentSignedOpenOrderNotional(
    const std::string& symbol,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    const double price = LastPrice(symbol, market);
    if (!(price > 0.0)) {
        return 0.0;
    }

    double notional = 0.0;
    for (const auto& order : account.open_orders) {
        if (order.symbol != symbol || !(order.quantity > 0.0)) {
            continue;
        }
        const double sign =
            (order.side == QTrading::Dto::Trading::OrderSide::Buy) ? 1.0 : -1.0;
        notional += order.quantity * price * sign;
    }
    return notional;
}

QTrading::Risk::RiskTarget CarryBasisHybridMultiPairRuntime::FreezePairToCurrentRiskTarget(
    const PairStaticInfo& pair,
    const QTrading::Risk::RiskTarget& reference,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    QTrading::Risk::RiskTarget out{};
    out.ts_ms = reference.ts_ms;
    out.strategy = reference.strategy.empty() ? "carry_basis_hybrid" : reference.strategy;
    out.max_leverage = reference.max_leverage;
    out.risk_budget_used = reference.risk_budget_used;
    out.target_positions[pair.spot_symbol] =
        CurrentSignedNotional(pair.spot_symbol, account, market) +
        CurrentSignedOpenOrderNotional(pair.spot_symbol, account, market);
    out.target_positions[pair.perp_symbol] =
        CurrentSignedNotional(pair.perp_symbol, account, market) +
        CurrentSignedOpenOrderNotional(pair.perp_symbol, account, market);

    const auto spot_lev = reference.leverage.find(pair.spot_symbol);
    if (spot_lev != reference.leverage.end()) {
        out.leverage[pair.spot_symbol] = spot_lev->second;
    }
    const auto perp_lev = reference.leverage.find(pair.perp_symbol);
    if (perp_lev != reference.leverage.end()) {
        out.leverage[pair.perp_symbol] = perp_lev->second;
    }
    return out;
}

void CarryBasisHybridMultiPairRuntime::ClampPairTargetDelta(
    const PairStaticInfo& pair,
    QTrading::Risk::RiskTarget& target,
    const MarketPtr& market) const
{
    if (!runtime_cfg_.carry_basis_delta_clamp_enabled) {
        return;
    }

    auto spot_it = target.target_positions.find(pair.spot_symbol);
    auto perp_it = target.target_positions.find(pair.perp_symbol);
    if (spot_it == target.target_positions.end() ||
        perp_it == target.target_positions.end())
    {
        return;
    }

    const double spot_price = LastPrice(pair.spot_symbol, market);
    const double perp_price = LastPrice(pair.perp_symbol, market);
    if (!(spot_price > 0.0) || !(perp_price > 0.0)) {
        return;
    }

    const double spot_target = spot_it->second;
    const double perp_target = perp_it->second;
    if (spot_target == 0.0 || perp_target == 0.0 || spot_target * perp_target >= 0.0) {
        return;
    }

    const double spot_base = std::abs(spot_target) / spot_price;
    const double perp_base = std::abs(perp_target) / perp_price;
    const double base_ref = std::max(spot_base, perp_base);
    if (!(base_ref > 0.0)) {
        return;
    }

    const double imbalance = std::abs(spot_base - perp_base) / base_ref;
    if (imbalance <= runtime_cfg_.carry_basis_delta_max_base_imbalance_ratio) {
        return;
    }

    const double clamped_base = std::min(spot_base, perp_base);
    spot_it->second = std::copysign(clamped_base * spot_price, spot_target);
    perp_it->second = std::copysign(clamped_base * perp_price, perp_target);
}

bool CarryBasisHybridMultiPairRuntime::PassesTurnoverEconomicsGate(
    const PairStaticInfo& pair,
    const QTrading::Risk::RiskTarget& target,
    const QTrading::Risk::AccountState& account,
    const MarketPtr& market) const
{
    if (!runtime_cfg_.carry_basis_turnover_gate_enabled) {
        return true;
    }

    const auto spot_target_it = target.target_positions.find(pair.spot_symbol);
    const auto perp_target_it = target.target_positions.find(pair.perp_symbol);
    if (spot_target_it == target.target_positions.end() ||
        perp_target_it == target.target_positions.end())
    {
        return true;
    }

    const double current_spot =
        CurrentSignedNotional(pair.spot_symbol, account, market) +
        CurrentSignedOpenOrderNotional(pair.spot_symbol, account, market);
    const double current_perp =
        CurrentSignedNotional(pair.perp_symbol, account, market) +
        CurrentSignedOpenOrderNotional(pair.perp_symbol, account, market);

    const double delta_turnover =
        std::abs(spot_target_it->second - current_spot) +
        std::abs(perp_target_it->second - current_perp);
    if (!(delta_turnover > 0.0)) {
        return true;
    }

    const double target_perp_abs = std::abs(perp_target_it->second);
    const double funding_rate = LatestFundingRate(pair.perp_symbol, market);
    const double expected_funding =
        target_perp_abs *
        std::max(0.0, funding_rate) *
        runtime_cfg_.carry_basis_turnover_expected_funding_settlements;
    const double expected_cost =
        delta_turnover * runtime_cfg_.carry_basis_turnover_cost_rate;
    if (!(expected_cost > 0.0)) {
        return true;
    }

    return expected_funding >=
        (expected_cost * runtime_cfg_.carry_basis_turnover_min_gain_to_cost);
}

void CarryBasisHybridMultiPairRuntime::RunOneCycle()
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

    std::unordered_set<std::size_t> active_indexes = exposed_pair_indexes;
    QTrading::Risk::RiskTarget merged_risk{};
    merged_risk.ts_ms = market ? market->Timestamp : 0;
    merged_risk.strategy = "carry_basis_hybrid";
    QTrading::Signal::SignalDecision execution_signal_src =
        MakeInactiveHybridSignal(market ? market->Timestamp : 0);

    for (const auto& ranked : ranked_pairs) {
        active_indexes.insert(ranked.pair_index);
        auto& pair_runtime = pair_runtime_states_[ranked.pair_index];
        auto intent = pair_runtime.intent_builder.build(ranked.signal, market);
        auto risk = risk_engine_.position(intent, account, market);
        if (ranked.portfolio_weight > 0.0) {
            risk = ScaleRiskTarget(risk, ranked.portfolio_weight);
        }
        const auto& pair = pair_static_infos_[ranked.pair_index];
        ClampPairTargetDelta(pair, risk, market);
        if (!PassesTurnoverEconomicsGate(pair, risk, account, market)) {
            risk = FreezePairToCurrentRiskTarget(pair, risk, account, market);
        }
        MergeRiskTarget(risk, merged_risk);
        if (execution_signal_src.status != QTrading::Signal::SignalStatus::Active ||
            ranked.signal.confidence > execution_signal_src.confidence) {
            execution_signal_src = ranked.signal;
        }
    }

    for (const auto pair_index : exposed_pair_indexes) {
        bool still_active = false;
        for (const auto& ranked : ranked_pairs) {
            if (ranked.pair_index == pair_index) {
                still_active = true;
                break;
            }
        }
        if (still_active || pair_index >= pair_static_infos_.size()) {
            continue;
        }
        const auto& pair = pair_static_infos_[pair_index];
        QTrading::Risk::RiskTarget flatten{};
        flatten.ts_ms = market ? market->Timestamp : 0;
        flatten.strategy = "carry_basis_hybrid";
        flatten.target_positions[pair.spot_symbol] = 0.0;
        flatten.target_positions[pair.perp_symbol] = 0.0;
        MergeRiskTarget(flatten, merged_risk);
    }

    const auto execution_signal = ToExecutionSignal(execution_signal_src);
    const auto orders = execution_orchestrator_.Execute(merged_risk, account, execution_signal, market);
    exchange_gateway_.SubmitOrders(orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Strategy
