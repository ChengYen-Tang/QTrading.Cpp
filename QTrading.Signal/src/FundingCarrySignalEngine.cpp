#include "Signal/FundingCarrySignalEngine.hpp"

namespace QTrading::Signal {

FundingCarrySignalEngine::FundingCarrySignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
}

bool FundingCarrySignalEngine::market_has_symbols(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    if (!market) {
        return false;
    }

    if (!has_symbol_ids_ && market->symbols) {
        const auto& symbols = *market->symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == cfg_.spot_symbol) {
                spot_id_ = i;
            }
            if (symbols[i] == cfg_.perp_symbol) {
                perp_id_ = i;
            }
        }
        has_symbol_ids_ = (spot_id_ < symbols.size() && perp_id_ < symbols.size());
    }

    if (has_symbol_ids_ &&
        spot_id_ < market->klines_by_id.size() &&
        perp_id_ < market->klines_by_id.size())
    {
        return market->klines_by_id[spot_id_].has_value() &&
            market->klines_by_id[perp_id_].has_value();
    }

    const auto& klines = market->klines;
    auto it_spot = klines.find(cfg_.spot_symbol);
    auto it_perp = klines.find(cfg_.perp_symbol);
    if (it_spot == klines.end() || it_perp == klines.end()) {
        return false;
    }
    return it_spot->second.has_value() && it_perp->second.has_value();
}

SignalDecision FundingCarrySignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out;
    if (!market) {
        return out;
    }

    // Funding carry uses a delta-neutral structure; the signal is a readiness check,
    // not a timing prediction. If spot/perp data exists, we keep the strategy active.
    out.ts_ms = market->Timestamp;
    out.symbol = cfg_.perp_symbol;
    out.strategy = "funding_carry";

    if (!market_has_symbols(market)) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    // Active by design: funding is earned over time, so urgency is low.
    out.status = SignalStatus::Active;
    out.confidence = 1.0;
    out.urgency = SignalUrgency::Low;
    return out;
}

} // namespace QTrading::Signal
