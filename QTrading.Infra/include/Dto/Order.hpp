#pragma once

#include <string>

namespace QTrading::dto {
    struct Order {
        int         id;               // Unique order ID
        std::string symbol;
        double      quantity;         // Remaining quantity to be matched
        double      price;            // <= 0 => market order, > 0 => limit order
        bool        is_long;
        bool        reduce_only;      // true means the order is for reducing positions only
        int         closing_position_id; // >=0: specifies which position to close; -1: normal opening order
    };
}
