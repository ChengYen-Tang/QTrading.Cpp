#pragma once

#include <string>

namespace QTrading::dto {

    /// @brief Data Transfer Object describing an order on the exchange.
    /// @details Contains order identity, symbol, quantity, price, direction, and related flags.
    struct Order {
        /// @brief Unique order identifier.
        int id;

        /// @brief Trading pair symbol (e.g., "BTCUSDT").
        std::string symbol;

        /// @brief Remaining quantity to be matched.
        double quantity;

        /// @brief Price: ≤ 0 ⇒ market order; > 0 ⇒ limit order.
        double price;

        /// @brief True if this is a long (buy) order; false if short (sell).
        bool is_long;

        /// @brief If true, this order only reduces existing positions.
        bool reduce_only;

        /// @brief For closing orders: ID of the position being closed; –1 if opening order.
        int closing_position_id;
    };

}  // namespace QTrading::dto
