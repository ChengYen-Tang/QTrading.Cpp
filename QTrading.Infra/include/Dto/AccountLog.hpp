#pragma once

namespace QTrading::dto {

    /// @brief Snapshot of account financial metrics at a point in time.
    /// @details Used for logging account state: balance, unrealized P&L, and total equity.
    struct AccountLog {
        /// @brief Current cash balance.
        double balance;

        /// @brief Unrealized profit and loss.
        double unreal_pnl;

        /// @brief Total equity (balance + unrealized P&L).
        double equity;
    };

}  // namespace QTrading::dto
