#pragma once

#include <string>

namespace QTrading::dto {

    /// @brief Data Transfer Object representing an open position.
    /// @details Tracks quantity, entry price, unrealized P&L, margins, fees, and leverage.
    struct Position {
        /// @brief Unique position identifier.
        int id;

        /// @brief ID of the order that opened this position.
        int order_id;

        /// @brief Trading pair symbol (e.g., "BTCUSDT").
        std::string symbol;

        /// @brief Current position size.
        double quantity;

        /// @brief Entry price for this position.
        double entry_price;

        /// @brief True if long; false if short.
        bool is_long;

        /// @brief Unrealized profit and loss.
        double unrealized_pnl;

        /// @brief Notional value (quantity × entry_price).
        double notional;

        /// @brief Initial margin allocated for this position.
        double initial_margin;

        /// @brief Maintenance margin requirement.
        double maintenance_margin;

        /// @brief Fees paid upon entry.
        double fee;

        /// @brief Leverage used.
        double leverage;

        /// @brief Fee rate applied (maker/taker).
        double fee_rate;
    };

}  // namespace QTrading::dto
