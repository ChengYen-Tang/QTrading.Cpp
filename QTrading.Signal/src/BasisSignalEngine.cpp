#include "Signal/BasisSignalEngine.hpp"

#include <cmath>

namespace QTrading::Signal {

BasisSignalEngine::BasisSignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

SignalDecision BasisSignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out;
    if (!market) {
        return out;
    }

    const auto& klines = market->klines;
    auto it_spot = klines.find(cfg_.spot_symbol);
    auto it_perp = klines.find(cfg_.perp_symbol);
    if (it_spot == klines.end() || it_perp == klines.end()) {
        return out;
    }
    if (!it_spot->second.has_value() || !it_perp->second.has_value()) {
        return out;
    }

    const auto& spot = it_spot->second.value();
    const auto& perp = it_perp->second.value();

    const double spread = perp.ClosePrice - spot.ClosePrice;
    spread_window_.push_back(spread);
    if (spread_window_.size() > cfg_.window) {
        spread_window_.pop_front();
    }

    out.ts_ms = market->Timestamp;
    out.symbol = cfg_.perp_symbol;
    out.strategy = "basis_zscore";

    if (spread_window_.size() < cfg_.window) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    const double m = mean(spread_window_);
    const double s = stddev(spread_window_, m);
    if (s <= 0.0) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    const double z = (spread - m) / s;
    const double az = std::fabs(z);
    out.confidence = az;
    if (az >= cfg_.enter_z) {
        out.status = SignalStatus::Active;
    }
    else if (az <= cfg_.exit_z) {
        out.status = SignalStatus::Inactive;
    }
    else {
        out.status = SignalStatus::Cooldown;
    }

    if (az >= cfg_.enter_z * 2.0) {
        out.urgency = SignalUrgency::High;
    }
    else if (az >= cfg_.enter_z) {
        out.urgency = SignalUrgency::Medium;
    }
    else {
        out.urgency = SignalUrgency::Low;
    }

    return out;
}

double BasisSignalEngine::mean(const std::deque<double>& values)
{
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double BasisSignalEngine::stddev(const std::deque<double>& values, double m)
{
    if (values.size() < 2) {
        return 0.0;
    }
    double acc = 0.0;
    for (double v : values) {
        const double d = v - m;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(values.size() - 1));
}

} // namespace QTrading::Signal
