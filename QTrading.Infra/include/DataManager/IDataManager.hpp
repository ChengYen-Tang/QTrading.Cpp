#pragma once

#include <concepts>
#include <string>
#include <QTrading.Infra/include/Dto/Market/Base.hpp>
#include <QTrading.Infra/include/Dto/Order.hpp>
#include <QTrading.Infra/include/Dto/Position.hpp>

using namespace QTrading::dto::market;
using namespace QTrading::dto;

namespace QTrading::DataManager {
	template<typename T>
	requires std::derived_from<T, BaseMarketDto>
    class IDataManager {
    public:
        virtual ~IDataManager() = default;

        /// <summary>
        /// Used to update internal state when new market data is received
        /// </summary>
        /// <param name="marketData"></param>
        virtual void updateMarketData(const T& marketData) = 0;

        /// <summary>
		/// Used to update internal state when new position status is received
        /// </summary>
        /// <param name="position"></param>
        virtual void updatePosition(const PositionDto& position) = 0;

        /// <summary>
        /// Provides query interface for strategy, risk and other modules
        /// </summary>
        /// <param name="symbol"></param>
        /// <returns></returns>
        virtual T& getMarketSnapshot(const std::string& symbol) const = 0;

        /// <summary>
        /// Provides query interface for risk and other modules
        /// </summary>
        /// <param name="symbol"></param>
        /// <returns></returns>
        virtual PositionDto& getPosition(const std::string& symbol) const = 0;
    };
}
