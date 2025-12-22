#pragma once

namespace QTrading::dto {

    /// @brief Legacy account log payload for file logging.
    struct AccountLog {
        double balance;
        double unreal_pnl;
        double equity;
    };

}
