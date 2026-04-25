#pragma once

namespace QTrading::Dto::Trading {

    /// @brief Order side (action): BUY opens/increases long exposure or closes short exposure.
    /// SELL opens/increases short exposure or closes long exposure.
    enum class OrderSide {
        Buy,
        Sell
    };

    /// @brief Position side (state): LONG or SHORT (Hedge mode). In one-way mode, exposure is net (BOTH).
    enum class PositionSide {
        Long,
        Short,
        Both
    };

    /// @brief Limit-order time in force semantics supported by the simulator.
    enum class TimeInForce {
        GTC,
        IOC,
        FOK,
        GTX
    };

}  // namespace QTrading::Dto::Trading
