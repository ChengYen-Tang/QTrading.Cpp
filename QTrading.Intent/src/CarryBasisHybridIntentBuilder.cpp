#include "Intent/CarryBasisHybridIntentBuilder.hpp"
#include "Intent/PairTradeIntentSupport.hpp"

#include <utility>

namespace QTrading::Intent {

CarryBasisHybridIntentBuilder::CarryBasisHybridIntentBuilder(Config cfg)
    : cfg_(std::move(cfg))
{
}

TradeIntent CarryBasisHybridIntentBuilder::build(
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    (void)market;
    TradeIntent out = QTrading::Intent::Support::BuildPairTradeIntentBase(
        signal,
        "carry_basis_hybrid",
        "delta_neutral_carry",
        "carry_basis_hybrid");
    out.strategy_kind = QTrading::Contracts::StrategyKind::FundingCarry;
    out.structure_kind = QTrading::Contracts::TradeStructureKind::DeltaNeutralCarry;

    if (signal.status != QTrading::Signal::SignalStatus::Active || !cfg_.receive_funding) {
        return out;
    }

    out.intent_id = QTrading::Intent::Support::BuildPairIntentId(
        "carry_basis_hybrid",
        cfg_.spot_symbol,
        cfg_.perp_symbol,
        true);
    QTrading::Intent::Support::ApplyPairLegDirection(
        out,
        cfg_.spot_symbol,
        cfg_.perp_symbol,
        true);
    return out;
}

} // namespace QTrading::Intent
