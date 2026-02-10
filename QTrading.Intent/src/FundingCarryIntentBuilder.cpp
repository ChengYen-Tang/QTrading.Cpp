#include "Intent/FundingCarryIntentBuilder.hpp"

namespace QTrading::Intent {

FundingCarryIntentBuilder::FundingCarryIntentBuilder(Config cfg)
    : cfg_(std::move(cfg))
{
}

TradeIntent FundingCarryIntentBuilder::build(const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    (void)market;
    TradeIntent out;
    out.ts_ms = signal.ts_ms;
    out.strategy = signal.strategy;
    out.structure = "delta_neutral_carry";
    out.position_mode = "hedge";
    out.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High) ? "high" :
        (signal.urgency == QTrading::Signal::SignalUrgency::Medium) ? "med" : "low";
    out.confidence = (signal.confidence > 0.0) ? signal.confidence : 1.0;
    out.reason = "funding_carry";

    if (signal.status != QTrading::Signal::SignalStatus::Active) {
        return out;
    }

    // Funding carry uses a delta-neutral pair. When receive_funding is true,
    // we go long spot and short perp (typical case when funding is positive).
    out.intent_id = "funding_carry:" + cfg_.spot_symbol + ":" + cfg_.perp_symbol +
        (cfg_.receive_funding ? ":receive" : ":pay");

    if (cfg_.receive_funding) {
        out.legs.push_back(TradeLeg{ cfg_.spot_symbol, TradeSide::Long });
        out.legs.push_back(TradeLeg{ cfg_.perp_symbol, TradeSide::Short });
    }
    else {
        out.legs.push_back(TradeLeg{ cfg_.spot_symbol, TradeSide::Short });
        out.legs.push_back(TradeLeg{ cfg_.perp_symbol, TradeSide::Long });
    }

    return out;
}

} // namespace QTrading::Intent
