#include "Signal/CarryBasisHybridSignalEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace {

double Clamp01(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

double ClampPositive(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value;
}

double ClampNonNegative(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

template <typename T>
void PushBounded(std::deque<T>& values, T value, std::size_t max_size)
{
    if (max_size == 0) {
        values.clear();
        return;
    }
    values.push_back(value);
    while (values.size() > max_size) {
        values.pop_front();
    }
}

} // namespace

namespace QTrading::Signal {

CarryBasisHybridSignalEngine::CarryBasisHybridSignalEngine(Config cfg)
    : cfg_(std::move(cfg))
    , funding_signal_(cfg_.funding_cfg)
    , basis_signal_(cfg_.basis_cfg)
{
    cfg_.funding_confidence_weight = Clamp01(cfg_.funding_confidence_weight);
    cfg_.basis_confidence_weight = Clamp01(cfg_.basis_confidence_weight);
    const double total_weight = cfg_.funding_confidence_weight + cfg_.basis_confidence_weight;
    if (total_weight <= 0.0) {
        cfg_.funding_confidence_weight = 1.0;
        cfg_.basis_confidence_weight = 0.0;
    }
    else {
        cfg_.funding_confidence_weight /= total_weight;
        cfg_.basis_confidence_weight /= total_weight;
    }
    cfg_.basis_inactive_confidence_scale = Clamp01(cfg_.basis_inactive_confidence_scale);
    cfg_.basis_active_boost_scale = ClampPositive(cfg_.basis_active_boost_scale, 1.0);
    cfg_.min_active_confidence = Clamp01(cfg_.min_active_confidence);
    cfg_.funding_regime_window_settlements =
        std::max<std::size_t>(1, cfg_.funding_regime_window_settlements);
    cfg_.funding_regime_min_samples =
        std::min(cfg_.funding_regime_min_samples, cfg_.funding_regime_window_settlements);
    cfg_.funding_regime_max_negative_share = Clamp01(cfg_.funding_regime_max_negative_share);
    cfg_.funding_regime_confidence_floor = Clamp01(cfg_.funding_regime_confidence_floor);
    cfg_.funding_allocator_reference_rate =
        ClampPositive(cfg_.funding_allocator_reference_rate, 0.00010);
    cfg_.funding_allocator_weight = ClampNonNegative(cfg_.funding_allocator_weight);
    cfg_.basis_allocator_weight = ClampNonNegative(cfg_.basis_allocator_weight);
}

bool CarryBasisHybridSignalEngine::funding_regime_allows_entry(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    double& confidence_scale)
{
    confidence_scale = 1.0;
    if (!cfg_.funding_regime_filter_enabled) {
        return true;
    }
    if (!QTrading::Signal::Support::ResolvePairSymbolIds(
        market,
        cfg_.funding_cfg.spot_symbol,
        cfg_.funding_cfg.perp_symbol,
        symbol_ids_))
    {
        confidence_scale = funding_regime_cached_confidence_scale_;
        return funding_regime_cached_allows_entry_;
    }
    if (!market ||
        symbol_ids_.perp_id >= market->funding_by_id.size() ||
        !market->funding_by_id[symbol_ids_.perp_id].has_value())
    {
        confidence_scale = funding_regime_cached_confidence_scale_;
        return funding_regime_cached_allows_entry_;
    }

    const auto& funding = *market->funding_by_id[symbol_ids_.perp_id];
    if (!has_last_funding_time_ || funding.FundingTime != last_funding_time_) {
        has_last_funding_time_ = true;
        last_funding_time_ = funding.FundingTime;
        PushBounded(
            funding_regime_rates_,
            funding.Rate,
            cfg_.funding_regime_window_settlements);
    }

    if (funding_regime_rates_.size() < cfg_.funding_regime_min_samples) {
        funding_regime_cached_allows_entry_ = true;
        funding_regime_cached_confidence_scale_ = 1.0;
        return true;
    }

    const double sum = std::accumulate(
        funding_regime_rates_.begin(),
        funding_regime_rates_.end(),
        0.0);
    const double mean = sum / static_cast<double>(funding_regime_rates_.size());
    const auto negative_count = std::count_if(
        funding_regime_rates_.begin(),
        funding_regime_rates_.end(),
        [](double value) { return value < 0.0; });
    const double negative_share =
        static_cast<double>(negative_count) /
        static_cast<double>(funding_regime_rates_.size());

    if (mean < cfg_.funding_regime_min_mean_rate ||
        negative_share > cfg_.funding_regime_max_negative_share)
    {
        confidence_scale = 0.0;
        funding_regime_cached_allows_entry_ = false;
        funding_regime_cached_confidence_scale_ = 0.0;
        return false;
    }

    if (mean > 0.0 && cfg_.funding_regime_min_mean_rate >= 0.0) {
        const double reference = std::max(5e-5, cfg_.funding_regime_min_mean_rate + 5e-5);
        const double mean_score = Clamp01(mean / reference);
        confidence_scale = std::max(
            cfg_.funding_regime_confidence_floor,
            mean_score);
    }
    funding_regime_cached_allows_entry_ = true;
    funding_regime_cached_confidence_scale_ = confidence_scale;
    return true;
}

SignalDecision CarryBasisHybridSignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    auto funding = funding_signal_.on_market(market);
    auto basis = basis_signal_.on_market(market);

    SignalDecision out{};
    out.ts_ms = market ? market->Timestamp : 0;
    out.symbol = !funding.symbol.empty() ? funding.symbol : basis.symbol;
    out.strategy = "carry_basis_hybrid";
    // Keep typed routing carry-like so risk/execution preserve funding-carry sizing semantics.
    out.strategy_kind = QTrading::Contracts::StrategyKind::FundingCarry;
    out.urgency = QTrading::Signal::SignalUrgency::Low;

    const bool funding_active = funding.status == SignalStatus::Active && funding.confidence > 0.0;
    const bool basis_active = basis.status == SignalStatus::Active && basis.confidence > 0.0;

    if (cfg_.require_funding_active && !funding_active) {
        out.status = SignalStatus::Inactive;
        return out;
    }
    if (!funding_active && !(cfg_.allow_basis_only_entry && basis_active)) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    double funding_regime_confidence_scale = 1.0;
    if (!funding_regime_allows_entry(market, funding_regime_confidence_scale)) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    out.status = SignalStatus::Active;
    const double funding_confidence = funding_active ? Clamp01(funding.confidence) : 0.0;
    const double basis_confidence = basis_active ? Clamp01(basis.confidence) : 0.0;

    double confidence =
        cfg_.funding_confidence_weight * funding_confidence +
        cfg_.basis_confidence_weight * basis_confidence;

    if (cfg_.basis_overlay_enabled) {
        if (basis_active) {
            confidence = std::min(1.0, confidence * cfg_.basis_active_boost_scale);
        }
        else {
            confidence *= cfg_.basis_inactive_confidence_scale;
        }
    }

    // Funding carry is a holding strategy.  Basis is an overlay for sizing and
    // ranking; a weak overlay must not force minute-level close/reopen churn.
    out.confidence = std::max(
        cfg_.min_active_confidence,
        Clamp01(confidence * funding_regime_confidence_scale));

    out.allocator_score =
        cfg_.basis_allocator_weight * std::max(0.0, basis.allocator_score);
    if (cfg_.funding_allocator_score_enabled &&
        QTrading::Signal::Support::ResolvePairSymbolIds(
            market,
            cfg_.funding_cfg.spot_symbol,
            cfg_.funding_cfg.perp_symbol,
            symbol_ids_) &&
        market &&
        symbol_ids_.perp_id < market->funding_by_id.size() &&
        market->funding_by_id[symbol_ids_.perp_id].has_value())
    {
        const double rate = market->funding_by_id[symbol_ids_.perp_id]->Rate;
        if (std::isfinite(rate) && rate > 0.0) {
            const double funding_score =
                std::tanh(rate / cfg_.funding_allocator_reference_rate);
            out.allocator_score +=
                cfg_.funding_allocator_weight * funding_score * out.confidence;
        }
    }
    return out;
}

} // namespace QTrading::Signal
