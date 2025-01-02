#pragma once

#include <string>

namespace QTrading::dto {
	struct OrderDto {
        std::string symbol;       // �X���N���A�Ҧp BTCUSDT
        long orderId;             // �q�� ID
        std::string orderType;    // �q�������A�Ҧp "LIMIT", "MARKET", "STOP"
        std::string side;         // �q���V�A�Ҧp "BUY", "SELL"
        double price;             // �]�w������]�ȭ������q��^
        double quantity;          // �]�w���ƶq
        std::string timeInForce;  // �ɮĵ����A�Ҧp "GTC", "IOC", "FOK"
        bool reduceOnly;          // �O�_���u��ܭq��
        std::string status;       // �q�檬�A�A�Ҧp "NEW", "FILLED", "CANCELED"
	};
}
