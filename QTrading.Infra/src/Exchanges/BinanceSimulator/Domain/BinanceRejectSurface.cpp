#include "Exchanges/BinanceSimulator/Domain/BinanceRejectSurface.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

std::pair<int, std::string> BinanceRejectSurface::MapToBinanceError(
    const std::optional<Contracts::OrderRejectInfo>& reject)
{
    using Code = Contracts::OrderRejectInfo::Code;
    if (!reject.has_value() || reject->code == Code::None) {
        return { 0, {} };
    }

    switch (reject->code) {
    case Code::UnknownSymbol:
        return { -1121, "Invalid symbol." };
    case Code::InvalidQuantity:
        return { -4003, "Quantity less than zero." };
    case Code::DuplicateClientOrderId:
        return { -4111, "DUPLICATED_CLIENT_TRAN_ID" };
    case Code::StpExpiredTaker:
    case Code::StpExpiredBoth:
        return { -2010, "Order would trigger self-trade prevention." };
    case Code::InvalidStpMode:
    case Code::StpModeNotAllowed:
        return { -1130, "Data sent for parameter 'selfTradePreventionMode' is not valid." };
    case Code::SpotHedgeModeUnsupported:
    case Code::HedgeModePositionSideRequired:
        return { -4061, "Order's position side does not match user's setting." };
    case Code::StrictHedgeReduceOnlyDisabled:
    case Code::ReduceOnlyNoReduciblePosition:
        return { -2022, "ReduceOnly Order is rejected." };
    case Code::PriceFilterBelowMin:
    case Code::PriceFilterAboveMax:
    case Code::PriceFilterInvalidTick:
        return { -1013, "Filter failure: PRICE_FILTER" };
    case Code::LotSizeBelowMinQty:
    case Code::LotSizeAboveMaxQty:
    case Code::LotSizeInvalidStep:
        return { -1013, "Filter failure: LOT_SIZE" };
    case Code::NotionalNoReferencePrice:
    case Code::NotionalBelowMin:
    case Code::NotionalAboveMax:
        return { -1013, "Filter failure: NOTIONAL" };
    case Code::PercentPriceAboveBound:
    case Code::PercentPriceBelowBound:
        return { -1013, "Filter failure: PERCENT_PRICE" };
    case Code::TriggerProtectExceeded:
        return { -2021, "Order would immediately trigger." };
    case Code::MarketTakeBoundExceeded:
        return { -4131, "The counterparty's best price does not meet the PERCENT_PRICE filter limit." };
    case Code::PostOnlyWouldTake:
        return { -2010, "Order would immediately match and take." };
    case Code::SpotReduceOnlyUnsupported:
        return { -1106, "Parameter sent when not required." };
    case Code::SpotInsufficientCash:
    case Code::PerpInsufficientMargin:
        return { -2019, "Margin is insufficient." };
    case Code::ClosePositionInvalidParameters:
        return { -1106, "Parameter sent when not required." };
    case Code::SpotNoInventory:
    case Code::SpotQuantityExceedsInventory:
    case Code::SpotNoLongPositionToClose:
    default:
        break;
    }

    return { -2010, "NEW_ORDER_REJECTED" };
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
