#include "Intent/PairTradeIntentSupport.hpp"

namespace QTrading::Intent::Support {

TradeIntent BuildPairTradeIntentBase(
    const QTrading::Signal::SignalDecision& signal,
    const std::string& strategy,
    const std::string& structure,
    const std::string& reason)
{
    TradeIntent out;
    out.ts_ms = signal.ts_ms;
    out.strategy = strategy;
    out.structure = structure;
    out.position_mode = "hedge";
    out.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High) ? "high" :
        (signal.urgency == QTrading::Signal::SignalUrgency::Medium) ? "med" : "low";
    out.confidence = (signal.confidence > 0.0) ? signal.confidence : 1.0;
    out.reason = reason;
    return out;
}

std::string BuildPairIntentId(
    const std::string& strategy,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    bool receive_funding)
{
    return strategy + ":" + spot_symbol + ":" + perp_symbol +
        (receive_funding ? ":receive" : ":pay");
}

void ApplyPairLegDirection(
    TradeIntent& intent,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    bool receive_funding)
{
    intent.legs.clear();
    if (receive_funding) {
        intent.legs.push_back(TradeLeg{ spot_symbol, TradeSide::Long });
        intent.legs.push_back(TradeLeg{ perp_symbol, TradeSide::Short });
    }
    else {
        intent.legs.push_back(TradeLeg{ spot_symbol, TradeSide::Short });
        intent.legs.push_back(TradeLeg{ perp_symbol, TradeSide::Long });
    }
}

} // namespace QTrading::Intent::Support
