#pragma once

#include <string>
#include "Dto/Trading/Side.hpp"

namespace QTrading::dto {

    /// @brief Data Transfer Object describing an order on the exchange.
    /// @details Contains order identity, symbol, quantity, price, action side, and related flags.
    struct Order {
        /// @brief Unique order identifier.
        int id;

        /// @brief Trading pair symbol (e.g., "BTCUSDT").
        std::string symbol;

        /// @brief Remaining quantity to be matched.
        double quantity;

        /// @brief Price: ≤ 0 ⇒ market order; > 0 ⇒ limit order.
        double price;

        /// @brief Order side (action): Buy or Sell.
        QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };

        /// @brief In hedge mode, indicates which position side this order targets.
        /// In one-way mode this should be PositionSide::Both.
        QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };

        /// @brief If true, this order only reduces existing positions.
        bool reduce_only{ false };

        /// @brief For internal closing orders: ID of the position being closed; –1 if opening order.
        int closing_position_id{ -1 };
    };

}  // namespace QTrading::dto
