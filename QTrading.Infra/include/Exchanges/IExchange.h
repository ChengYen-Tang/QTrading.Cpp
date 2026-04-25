#pragma once

#include <memory>
#include <string>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Global.hpp"
#include "Queue/Channel.hpp"

namespace QTrading::Infra::Exchanges {
	/// @brief Interface for a generic exchange, parameterized on market data type.
	/// @tparam TMarket DTO type for market snapshots (e.g., MultiKlineDto).
    template<typename TMarket>
    class IExchange {
	public:
		/// @brief Get the channel for market data snapshots.
		/// @return Shared pointer to the market data channel.
		std::shared_ptr<QTrading::Utils::Queue::Channel<TMarket>>                         get_market_channel()   const { return market_channel; }
		/// @brief Get the channel for publishing position updates.
		/// @return Shared pointer to the position channel.
		std::shared_ptr<QTrading::Utils::Queue::Channel<std::vector<QTrading::dto::Position>>>           get_position_channel() const { return position_channel; }
		/// @brief Get the channel for publishing order updates.
		/// @return Shared pointer to the order channel.
		std::shared_ptr<QTrading::Utils::Queue::Channel<std::vector<QTrading::dto::Order>>>              get_order_channel()    const { return order_channel; }

		/// @brief Advance the simulation by one step (e.g., one time tick).
		/// @return True if new market data was emitted; false if data is exhausted.
		virtual bool step() = 0;

		/// @brief Retrieve a snapshot of all current positions.
		/// @return Reference to a vector of Position DTOs.
		virtual const std::vector<QTrading::dto::Position>& get_all_positions()   const = 0;
		/// @brief Retrieve a snapshot of all open orders.
		/// @return Reference to a vector of Order DTOs.
		virtual const std::vector<QTrading::dto::Order>& get_all_open_orders() const = 0;

		/// @brief Set leverage for a symbol (optional override).
		/// @param symbol Trading symbol.
		/// @param new_leverage Desired leverage (>0).
		virtual void set_symbol_leverage(const std::string& symbol, double new_leverage)
		{
			(void)symbol;
			(void)new_leverage;
		}

		/// @brief Get leverage for a symbol (optional override).
		/// @param symbol Trading symbol.
		/// @return Current leverage (default 1.0 if unsupported).
		virtual double get_symbol_leverage(const std::string& symbol) const
		{
			(void)symbol;
			return 1.0;
		}

		/// @brief Close all public channels (market, positions, orders).
		virtual void close()
		{
			/// Default implementation: close each channel if still open.
			if (market_channel && !market_channel->IsClosed())   market_channel->Close();
			if (position_channel && !position_channel->IsClosed()) position_channel->Close();
			if (order_channel && !order_channel->IsClosed())    order_channel->Close();
		}

	protected:
		std::shared_ptr<QTrading::Utils::Queue::Channel<TMarket>>             market_channel;      ///< Channel for market snapshots.
		std::shared_ptr<QTrading::Utils::Queue::Channel<std::vector<QTrading::dto::Position>>> position_channel;  ///< Channel for position updates.
		std::shared_ptr<QTrading::Utils::Queue::Channel<std::vector<QTrading::dto::Order>>>    order_channel;     ///< Channel for order updates.

		/// @brief Update the global timestamp for logging.
		/// @param ts New timestamp value (e.g., milliseconds since epoch).
		void set_global_timestamp(unsigned long long &ts) {
			QTrading::Utils::GlobalTimestamp.store(ts, std::memory_order_release);
		}
    };
}
