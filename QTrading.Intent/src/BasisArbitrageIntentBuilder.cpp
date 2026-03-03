#include "Intent/BasisArbitrageIntentBuilder.hpp"

#include <string_view>

namespace QTrading::Intent {

BasisArbitrageIntentBuilder::BasisArbitrageIntentBuilder(Config cfg)
    : FundingCarryIntentBuilder(std::move(cfg))
{
}

TradeIntent BasisArbitrageIntentBuilder::build(
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    TradeIntent out = FundingCarryIntentBuilder::build(signal, market);
    out.strategy = "basis_arbitrage";
    out.reason = "basis_arbitrage";
    if (!out.intent_id.empty()) {
        constexpr std::string_view old_prefix = "funding_carry:";
        if (out.intent_id.rfind(old_prefix, 0) == 0) {
            out.intent_id.replace(0, old_prefix.size(), "basis_arbitrage:");
        }
    }
    return out;
}

} // namespace QTrading::Intent
