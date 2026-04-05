#pragma once

#include <string_view>

namespace QTrading::Contracts {

enum class StrategyKind {
    Unknown,
    FundingCarry,
    BasisArbitrage
};

enum class TradeStructureKind {
    Unknown,
    DeltaNeutralCarry,
    DeltaNeutralBasis
};

inline StrategyKind StrategyKindFromString(std::string_view strategy)
{
    if (strategy == "funding_carry") {
        return StrategyKind::FundingCarry;
    }
    if (strategy == "basis_arbitrage") {
        return StrategyKind::BasisArbitrage;
    }
    return StrategyKind::Unknown;
}

inline TradeStructureKind TradeStructureKindFromString(std::string_view structure)
{
    if (structure == "delta_neutral_carry") {
        return TradeStructureKind::DeltaNeutralCarry;
    }
    if (structure == "delta_neutral_basis") {
        return TradeStructureKind::DeltaNeutralBasis;
    }
    return TradeStructureKind::Unknown;
}

inline StrategyKind ResolveStrategyKind(StrategyKind typed_kind, std::string_view strategy)
{
    return (typed_kind != StrategyKind::Unknown)
        ? typed_kind
        : StrategyKindFromString(strategy);
}

inline TradeStructureKind ResolveTradeStructureKind(
    TradeStructureKind typed_kind,
    std::string_view structure)
{
    return (typed_kind != TradeStructureKind::Unknown)
        ? typed_kind
        : TradeStructureKindFromString(structure);
}

inline bool IsCarryLikeStrategy(StrategyKind kind)
{
    return kind == StrategyKind::FundingCarry || kind == StrategyKind::BasisArbitrage;
}

} // namespace QTrading::Contracts
