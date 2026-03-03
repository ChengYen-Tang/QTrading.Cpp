#include "Signal/BasisArbitrageSignalEngine.hpp"

namespace QTrading::Signal {

BasisArbitrageSignalEngine::BasisArbitrageSignalEngine(Config cfg)
    : FundingCarrySignalEngine(std::move(cfg))
{
}

SignalDecision BasisArbitrageSignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out = FundingCarrySignalEngine::on_market(market);
    out.strategy = "basis_arbitrage";
    return out;
}

} // namespace QTrading::Signal
