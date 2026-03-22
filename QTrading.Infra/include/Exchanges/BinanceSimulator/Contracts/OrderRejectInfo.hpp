#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct OrderRejectInfo {
    enum class Code {
        None = 0,
        Unknown = 1,
        NotImplemented = 2,
        UnknownSymbol = 3,
        InvalidQuantity = 4,
        DuplicateClientOrderId = 5,
        StpExpiredTaker = 6,
        StpExpiredBoth = 7,
        SpotHedgeModeUnsupported = 8,
        HedgeModePositionSideRequired = 9,
        StrictHedgeReduceOnlyDisabled = 10,
        ReduceOnlyNoReduciblePosition = 11,
        PriceFilterBelowMin = 12,
        PriceFilterAboveMax = 13,
        PriceFilterInvalidTick = 14,
        LotSizeBelowMinQty = 15,
        LotSizeAboveMaxQty = 16,
        LotSizeInvalidStep = 17,
        NotionalNoReferencePrice = 18,
        NotionalBelowMin = 19,
        NotionalAboveMax = 20,
        PercentPriceAboveBound = 21,
        PercentPriceBelowBound = 22,
        SpotInsufficientCash = 23,
        SpotNoInventory = 24,
        SpotQuantityExceedsInventory = 25,
        SpotNoLongPositionToClose = 26,
    };

    Code code{ Code::None };
    const char* message{ nullptr };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
