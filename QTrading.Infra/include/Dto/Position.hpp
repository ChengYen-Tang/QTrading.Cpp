#pragma once

#include <string>

namespace QTrading::dto {
    struct Position {
        int         id;
        int         order_id;       // The order that originally opened this position
        std::string symbol;
        double      quantity;
        double      entry_price;
        bool        is_long;
        double      unrealized_pnl;
        double      notional;
        double      initial_margin;
        double      maintenance_margin;
        double      fee;
        double      leverage;
        double      fee_rate;
    };
}
