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

        // ���ѭq�\�����ƾڪ�����(�^�էΦ�)
        // e.g. �� lambda/std::function ���~�������污/�q��^���ƥ�
        virtual void updateMarketDataCallback(
            std::function<void(const T&)> onMarketDataUpdate) = 0;

        virtual void updatePositionCallback(
            std::function<void(const PositionDto&)> onPositionUpdate) = 0;

        // ���ѵo�e�q�浹����Ҫ�����
        virtual void sendOrder(const OrderDto& orderReq) = 0;
    };
}
