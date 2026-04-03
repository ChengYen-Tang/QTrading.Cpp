#pragma once

#include <cstdint>
#include <string>
#include "Dto/Trading/Side.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"

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

        /// @brief Instrument type captured at order creation for routing/logging.
        QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };

        /// @brief Optional client-provided order id. Must be unique among open orders when provided.
        std::string client_order_id;

        /// @brief STP mode associated with this order (0=None, 1=ExpireTaker, 2=ExpireMaker, 3=ExpireBoth).
        int stp_mode{ 0 };

        /// @brief If true, this order originated from closePosition-style close-all intent.
        bool close_position{ false };

        /// @brief Original quote-order amount for spot market quoteOrderQty style requests.
        double quote_order_qty{ 0.0 };

        /// @brief Internal flag for one-way overshoot reverse orders pending close->open transition.
        bool one_way_reverse{ false };

        /// @brief Limit-order time in force; market-style orders ignore this field.
        QTrading::Dto::Trading::TimeInForce time_in_force{ QTrading::Dto::Trading::TimeInForce::GTC };

        /// @brief First replay step when this order is eligible to match.
        uint64_t first_matching_step{ 0 };
    };

}  // namespace QTrading::dto
