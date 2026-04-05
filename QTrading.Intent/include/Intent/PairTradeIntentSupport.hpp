#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Intent/TradeIntent.hpp"
#include "Signal/SignalDecision.hpp"

#include <string>

namespace QTrading::Intent::Support {

TradeIntent BuildPairTradeIntentBase(
    const QTrading::Signal::SignalDecision& signal,
    const std::string& strategy,
    const std::string& structure,
    const std::string& reason);

std::string BuildPairIntentId(
    const std::string& strategy,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    bool receive_funding);

void ApplyPairLegDirection(
    TradeIntent& intent,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    bool receive_funding);

} // namespace QTrading::Intent::Support
