#pragma once

#include <string>

namespace QTrading::dto {
	struct PositionDto {
		std::string symbol;
        double positionAmount;
        double entryPrice;
        double markPrice;
        double unrealizedPnL;
        double leverage;
        bool isolated;
        std::string positionSide;
	};
}
