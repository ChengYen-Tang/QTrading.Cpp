#pragma once

#include <string>

namespace QTrading::dto {
	struct PositionDto {
		std::string symbol;
        double positionAmount;    // 计秖タ计繷璽计繷
        double entryPrice;        // 秨基
        double markPrice;         // 讽玡夹癘基
        double unrealizedPnL;     // ゼ龟瞷莲
        double leverage;          // 膘计
        bool isolated;            // 琌硋家Α
        std::string positionSide; // よㄒ "BOTH", "LONG", "SHORT"
	};
}
