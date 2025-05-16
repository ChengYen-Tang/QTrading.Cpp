#pragma once

namespace QTrading::dto {
    struct AccountLog {
        double  balance;
        double  unreal_pnl;
        double  equity;      // balance + unreal
    };
}
