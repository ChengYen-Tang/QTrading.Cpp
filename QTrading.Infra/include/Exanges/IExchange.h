#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Queue/Channel.hpp"

using namespace QTrading::dto;
using namespace QTrading::Utils::Queue;

namespace QTrading::Infra::Exanges {
    template<typename TMarket>
    //requires std::derived_from<T, BaseMarketDto>
    class IExchange {
	public:
		std::shared_ptr<Channel<TMarket>>                         get_market_channel()   const { return market_channel; }
		std::shared_ptr<Channel<std::vector<Position>>>           get_position_channel() const { return position_channel; }
		std::shared_ptr<Channel<std::vector<Order>>>              get_order_channel()    const { return order_channel; }

		virtual void place_order(const std::string& symbol,
			double quantity,
			double price,
			bool is_long,
			bool reduce_only = false) = 0;

		virtual bool step() = 0;

		virtual const std::vector<Position>& get_all_positions()   const = 0;
		virtual const std::vector<Order>& get_all_open_orders() const = 0;

		virtual void close()
		{
			/*  default: just close every public channel  */
			if (market_channel && !market_channel->IsClosed())   market_channel->Close();
			if (position_channel && !position_channel->IsClosed()) position_channel->Close();
			if (order_channel && !order_channel->IsClosed())    order_channel->Close();
		}
	protected:
		std::shared_ptr<Channel<TMarket>>             market_channel;
		std::shared_ptr<Channel<std::vector<Position>>> position_channel;
		std::shared_ptr<Channel<std::vector<Order>>>    order_channel;
    };
}
