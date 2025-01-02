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

        // ����s�������ƾڮɡA�Ψӧ�s�������A
        virtual void updateMarketData(const T& marketData) = 0;

        // �����������즨��^��/�����ܰʮɡA��s�w�s
        virtual void updatePosition(const PositionDto& position) = 0;

        // ���ѵ������B���I���Ҳժ��d�ߤ���
        virtual T& getMarketSnapshot(const std::string& symbol) const = 0;
        virtual PositionDto& getPosition(const std::string& symbol) const = 0;
    };
}
