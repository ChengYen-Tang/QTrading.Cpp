#pragma once

#include <concepts>
#include <functional>
#include <Memory>
#include <string>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Queue/Channel.hpp"

using namespace QTrading::dto;

namespace QTrading::Infra::Exanges {
    template<typename T>
    //requires std::derived_from<T, BaseMarketDto>
    class IExchange {
	public:
		shared_ptr<Channel<T>> get_market_channel() const {
			return market_channel;
		}
		virtual void place_order(const std::string& symbol,
			double quantity,
			double price,
			bool is_long,
			bool reduce_only = false) = 0;
	protected:
		shared_ptr<Channel<T>> market_channel;
		shared_ptr<Channel<Position>> Position_channel;
    };
}
