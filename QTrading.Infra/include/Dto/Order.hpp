#pragma once

#include <string>

namespace QTrading::dto {
	struct OrderDto {
        std::string symbol;
        long orderId;
        std::string orderType;
        std::string side;
        double price;
        double quantity;
        std::string timeInForce;
        bool reduceOnly;
        std::string status;
	};
}
