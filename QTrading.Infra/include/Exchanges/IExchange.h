#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Dto/Trading/Side.hpp"
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

		/// @brief Place a new order.
		/// @param symbol Trading symbol.
		/// @param quantity Order quantity.
		/// @param price Limit price (>0) or market (<=0).
		/// @param side Order action side (Buy/Sell).
		/// @param position_side Target position side (Hedge mode: Long/Short; One-way: Both).
		/// @param reduce_only If true, this order must not increase exposure.
		virtual bool place_order(const std::string& symbol,
			double quantity,
			double price,
			QTrading::Dto::Trading::OrderSide side,
			QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
			bool reduce_only = false) = 0;

		/// @brief Place a new market order (price = 0).
		virtual bool place_order(const std::string& symbol,
			double quantity,
			QTrading::Dto::Trading::OrderSide side,
			QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
			bool reduce_only = false) = 0;

		/// @brief Close position(s) for a symbol at limited price.
		///        In one-way mode: closes net exposure. In hedge mode: closes both sides.
		virtual void close_position(const std::string& symbol, double price) = 0;
		/// @brief Close all positions for a symbol at market price.
		virtual void close_position(const std::string& symbol) = 0;

		/// @brief In hedge mode, close only one side (Long/Short) at price.
		///        In one-way mode, position_side must be Both.
		virtual void close_position(const std::string& symbol,
			QTrading::Dto::Trading::PositionSide position_side,
			double price = 0.0) = 0;

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

		/// @brief Cancel all open orders for a symbol (optional override).
		virtual void cancel_open_orders(const std::string& symbol)
		{
			(void)symbol;
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
