#pragma once

#include <concepts>
#include <functional>
#include <QTrading.Infra/include/dto/market/base.h>
#include <QTrading.Infra/include/dto/order.h>
#include <QTrading.Infra/include/dto/position.h>

using namespace QTrading::dto::market;
using namespace QTrading::dto;

namespace QTrading::exanges {
    template<typename T>
    requires std::derived_from<T, BaseMarketDto>
    class IDataFeed {
    public:
        virtual ~IDataFeed() = default;

        // 提供訂閱市場數據的介面(回調形式)
        // e.g. 用 lambda/std::function 讓外部接收行情/訂單回報事件
        virtual void updateMarketDataCallback(
            std::function<void(const T&)> onMarketDataUpdate) = 0;

        virtual void updatePositionCallback(
            std::function<void(const PositionDto&)> onPositionUpdate) = 0;

        // 提供發送訂單給交易所的介面
        virtual void sendOrder(const OrderDto& orderReq) = 0;
    };
}
