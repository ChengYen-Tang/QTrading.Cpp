#pragma once

#include <concepts>
#include <functional>
#include <QTrading.Infra/include/Dto/Market/Base.hpp>
#include <QTrading.Infra/include/Dto/Order.hpp>
#include <QTrading.Infra/include/Dto/Position.hpp>

using namespace QTrading::dto::market;
using namespace QTrading::dto;

namespace QTrading::exanges {
    template<typename T>
    requires std::derived_from<T, BaseMarketDto>
    class IDataFeed {
    public:
        virtual ~IDataFeed() = default;

        /// <summary>
        /// Provides an interface for subscribing to market data (callback form)
        /// </summary>
        /// <param name="onMarketDataUpdate"></param>
        virtual void updateMarketDataCallback(
            std::function<void(const T&)> onMarketDataUpdate) = 0;

        /// <summary>
        /// Provides an interface for subscribing when the position is changed (callback form)
        /// </summary>
        /// <param name="onPositionUpdate"></param>
        virtual void updatePositionCallback(
            std::function<void(const PositionDto&)> onPositionUpdate) = 0;

        /// <summary>
        /// Provides an interface for sending orders to exchanges
        /// </summary>
        /// <param name="orderReq"></param>
        virtual void sendOrder(const OrderDto& orderReq) = 0;
    };
}
