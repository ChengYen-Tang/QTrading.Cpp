#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Global.hpp"
#include "Queue/Channel.hpp"

using namespace QTrading::dto;
using namespace QTrading::Utils::Queue;

namespace QTrading::Infra::Exchanges {
	/// @brief Interface for a generic exchange, parameterized on market data type.
	/// @tparam TMarket DTO type for market snapshots (e.g., MultiKlineDto).
    template<typename TMarket>
    class IExchange {
	public:
		/// @brief Get the channel for market data snapshots.
		/// @return Shared pointer to the market data channel.
		std::shared_ptr<Channel<TMarket>>                         get_market_channel()   const { return market_channel; }
		/// @brief Get the channel for publishing position updates.
		/// @return Shared pointer to the position channel.
		std::shared_ptr<Channel<std::vector<Position>>>           get_position_channel() const { return position_channel; }
		/// @brief Get the channel for publishing order updates.
		/// @return Shared pointer to the order channel.
		std::shared_ptr<Channel<std::vector<Order>>>              get_order_channel()    const { return order_channel; }

		/// @brief Place a new order. (Limited Price)
		/// @param symbol     Trading symbol.
		/// @param quantity   Order quantity.
		/// @param price      Limit price (>0) or market (≤0).
		/// @param is_long    True for buy/long, false for sell/short.
		/// @param reduce_only If true, only reduce an existing position.
		virtual void place_order(const std::string& symbol,
			double quantity,
			double price,
			bool is_long,
			bool reduce_only = false) = 0;

		/// @brief Place a new order. (Market Price)
		/// @param symbol     Trading symbol.
		/// @param quantity   Order quantity.
		/// @param is_long    True for long, false for short.
		/// @param reduce_only If true, reduce-only.
		virtual void place_order(const std::string& symbol, double quantity, bool is_long, bool reduce_only = false) = 0;

		/// @brief Close position(s) for a symbol at limited price.
		///        In one-way mode: closes all. In hedge mode: customizable.
		/// @param symbol Trading symbol.
		/// @param price  Limit price (>0) or market (≤0).
		virtual void close_position(const std::string& symbol, double price) = 0;
		/// @brief Close all positions for a symbol at market price.
		/// @param symbol Trading symbol.
		virtual void close_position(const std::string& symbol) = 0;

		/// @brief In hedge mode, close only one side (long/short) at price.
		/// @param symbol Trading symbol.
		/// @param is_long True to close long side, false to close short.
		/// @param price   Limit price (>0) or market (≤0).
		virtual void close_position(const std::string& symbol, bool is_long, double price = 0.0) = 0;

		/// @brief Advance the simulation by one step (e.g., one time tick).
		/// @return True if new market data was emitted; false if data is exhausted.
		virtual bool step() = 0;

		/// @brief Retrieve a snapshot of all current positions.
		/// @return Reference to a vector of Position DTOs.
		virtual const std::vector<Position>& get_all_positions()   const = 0;
		/// @brief Retrieve a snapshot of all open orders.
		/// @return Reference to a vector of Order DTOs.
		virtual const std::vector<Order>& get_all_open_orders() const = 0;

		/// @brief Close all public channels (market, positions, orders).
		virtual void close()
		{
			/// Default implementation: close each channel if still open.
			if (market_channel && !market_channel->IsClosed())   market_channel->Close();
			if (position_channel && !position_channel->IsClosed()) position_channel->Close();
			if (order_channel && !order_channel->IsClosed())    order_channel->Close();
		}
	protected:
		std::shared_ptr<Channel<TMarket>>             market_channel;      ///< Channel for market snapshots.
		std::shared_ptr<Channel<std::vector<Position>>> position_channel;  ///< Channel for position updates.
		std::shared_ptr<Channel<std::vector<Order>>>    order_channel;     ///< Channel for order updates.

		/// @brief Update the global timestamp for logging.
		/// @param ts New timestamp value (e.g., milliseconds since epoch).
		void set_global_timestamp(unsigned long long &ts) {
			QTrading::Utils::GlobalTimestamp.store(ts, std::memory_order_release);
		}
    };
}
