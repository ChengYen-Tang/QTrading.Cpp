#pragma once

namespace QTrading::dto {

    /// @brief Legacy account log payload for file logging.
    struct AccountLog {
        // Legacy fields (perp-oriented) kept for compatibility with existing reports.
        double balance{ 0.0 };
        double unreal_pnl{ 0.0 };
        double equity{ 0.0 };

        // Perp ledger snapshot.
        double perp_wallet_balance{ 0.0 };
        double perp_available_balance{ 0.0 };
        double perp_ledger_value{ 0.0 };

        // Spot ledger snapshot.
        double spot_cash_balance{ 0.0 };
        double spot_available_balance{ 0.0 };
        double spot_inventory_value{ 0.0 };
        double spot_ledger_value{ 0.0 };

        // Combined account value.
        double total_cash_balance{ 0.0 };
        double total_ledger_value{ 0.0 };
    };

}
