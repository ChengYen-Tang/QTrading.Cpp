#pragma once

namespace QTrading::dto {

    /// @brief Legacy account log payload for file logging.
    struct AccountLog {
        double balance;
        double unreal_pnl;
        double equity;
    };

}

namespace QTrading::Dto::Account {

    /// @brief Cross-margin account snapshot (Binance-like fields).
    struct BalanceSnapshot {
        double WalletBalance{ 0.0 };
        double UnrealizedPnl{ 0.0 };
        double MarginBalance{ 0.0 };

        double PositionInitialMargin{ 0.0 };
        double OpenOrderInitialMargin{ 0.0 };
        double AvailableBalance{ 0.0 };

        double MaintenanceMargin{ 0.0 };
        double Equity{ 0.0 };
    };

}  // namespace QTrading::Dto::Account
