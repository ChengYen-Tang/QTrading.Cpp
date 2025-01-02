#pragma once

#include <concepts>
#include <string>
#include <QTrading.Infra/include/dto/market/base.h>
#include <QTrading.Infra/include/dto/order.h>
#include <QTrading.Infra/include/dto/position.h>

using namespace QTrading::dto::market;
using namespace QTrading::dto;

namespace QTrading::DataManager {
	template<typename T>
	requires std::derived_from<T, BaseMarketDto>
    class IDataManager {
    public:
        virtual ~IDataManager() = default;

        // 當收到新的市場數據時，用來更新內部狀態
        virtual void updateMarketData(const T& marketData) = 0;

        // 當執行引擎收到成交回報/持倉變動時，更新庫存
        virtual void updatePosition(const PositionDto& position) = 0;

        // 提供給策略、風險等模組的查詢介面
        virtual T& getMarketSnapshot(const std::string& symbol) const = 0;
        virtual PositionDto& getPosition(const std::string& symbol) const = 0;
    };
}
