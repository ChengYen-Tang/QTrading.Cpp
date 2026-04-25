#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Intent/PairTradeIntentSupport.hpp"

namespace QTrading::Intent {

FundingCarryIntentBuilder::FundingCarryIntentBuilder(Config cfg)
    : cfg_(std::move(cfg))
{
}

TradeIntent FundingCarryIntentBuilder::build(const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    (void)market;
    TradeIntent out = QTrading::Intent::Support::BuildPairTradeIntentBase(
        signal,
        signal.strategy,
        "delta_neutral_carry",
        "funding_carry");
    out.strategy_kind = QTrading::Contracts::ResolveStrategyKind(signal.strategy_kind, signal.strategy);
    out.structure_kind = QTrading::Contracts::TradeStructureKind::DeltaNeutralCarry;

    if (signal.status != QTrading::Signal::SignalStatus::Active) {
        return out;
    }

    // Funding carry uses a delta-neutral pair. When receive_funding is true,
    // we go long spot and short perp (typical case when funding is positive).
    out.intent_id = QTrading::Intent::Support::BuildPairIntentId(
        "funding_carry",
        cfg_.spot_symbol,
        cfg_.perp_symbol,
        cfg_.receive_funding);
    QTrading::Intent::Support::ApplyPairLegDirection(
        out,
        cfg_.spot_symbol,
        cfg_.perp_symbol,
        cfg_.receive_funding);

    return out;
}

} // namespace QTrading::Intent
