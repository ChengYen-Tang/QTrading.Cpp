#pragma once

#include <string>

namespace QTrading::dto {
	struct OrderDto {
        std::string symbol;       // 合約代號，例如 BTCUSDT
        long orderId;             // 訂單 ID
        std::string orderType;    // 訂單類型，例如 "LIMIT", "MARKET", "STOP"
        std::string side;         // 訂單方向，例如 "BUY", "SELL"
        double price;             // 設定的價格（僅限限價訂單）
        double quantity;          // 設定的數量
        std::string timeInForce;  // 時效策略，例如 "GTC", "IOC", "FOK"
        bool reduceOnly;          // 是否為只減倉訂單
        std::string status;       // 訂單狀態，例如 "NEW", "FILLED", "CANCELED"
	};
}
