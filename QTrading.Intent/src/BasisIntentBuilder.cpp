#include "Intent/BasisIntentBuilder.hpp"

namespace QTrading::Intent {

BasisIntentBuilder::BasisIntentBuilder(Config cfg)
    : cfg_(std::move(cfg))
{
}

TradeIntent BasisIntentBuilder::build(const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    (void)market;
    TradeIntent out;
    out.ts_ms = signal.ts_ms;
    out.strategy = signal.strategy;
    out.structure = "delta_neutral_basis";
    out.position_mode = "hedge";
    out.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High) ? "high" :
        (signal.urgency == QTrading::Signal::SignalUrgency::Medium) ? "med" : "low";
    out.reason = "basis_zscore";

    if (signal.status != QTrading::Signal::SignalStatus::Active) {
        return out;
    }

    out.intent_id = "basis:" + cfg_.spot_symbol + ":" + cfg_.perp_symbol;
    out.legs.push_back(TradeLeg{ cfg_.spot_symbol, TradeSide::Long });
    out.legs.push_back(TradeLeg{ cfg_.perp_symbol, TradeSide::Short });
    return out;
}

} // namespace QTrading::Intent
