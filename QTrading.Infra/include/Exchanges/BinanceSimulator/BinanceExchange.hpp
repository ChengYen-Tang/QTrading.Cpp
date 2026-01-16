#pragma once

#include <unordered_map>
#include <vector>
#include <optional>
#include <queue>
#include <cstdint>

#include "Exchanges/IExchange.h"
#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Trading/Side.hpp"
#include "Queue/ChannelFactory.hpp"
#include "Logger.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

    using MultiKlinePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    /// @brief Simulator for Binance exchange:
    ///  - Loads per-symbol CSVs via MarketData.
    ///  - On step(), emits the earliest timestamp snapshot across all symbols.
    ///    Missing symbols at that timestamp yield std::nullopt entries.
    ///  - Debounces position/order channels: only sends updates when state changes.
    class BinanceExchange final : public QTrading::Infra::Exchanges::IExchange<MultiKlinePtr> {
    public:
        /// @brief Construct with CSV mappings, logger, initial balance and VIP level.
        /// @param symbolCsv Vector of (symbol, csv_file) pairs.
        /// @param logger Shared pointer to a Logger instance.
        /// @param init_balance Starting account balance.
        /// @param vip_level VIP level for fee tier (0–9).
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            std::shared_ptr<QTrading::Log::Logger> logger,
            double  init_balance = 1'000'000.0,
            int     vip_level = 0);

        /// @brief Construct with CSV mappings, logger, and existing Account.
        /// @param symbolCsv Vector of (symbol, csv_file) pairs.
        /// @param logger Shared pointer to a Logger instance.
        /// @param account Shared pointer to a preconfigured Account.
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            std::shared_ptr<QTrading::Log::Logger> logger,
            std::shared_ptr<Account> account);

        /// @copydoc IExchange::place_order
        bool place_order(const std::string& symbol,
            double quantity,
            double price,
            QTrading::Dto::Trading::OrderSide side,
            QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
            bool reduce_only = false) override;
        /// @copydoc IExchange::place_order
        bool place_order(const std::string& symbol,
            double quantity,
            QTrading::Dto::Trading::OrderSide side,
            QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
            bool reduce_only = false) override;

        /// @copydoc IExchange::close_position
        void close_position(const std::string& symbol, double price) override;
        /// @copydoc IExchange::close_position
        void close_position(const std::string& symbol) override;
        /// @copydoc IExchange::close_position
        void close_position(const std::string& symbol,
            QTrading::Dto::Trading::PositionSide position_side,
            double price = 0.0) override;

        /// @copydoc IExchange::step
        bool  step() override;

        /// @copydoc IExchange::get_all_positions
        const std::vector<dto::Position>& get_all_positions()   const override;
        /// @copydoc IExchange::get_all_open_orders
        const std::vector<dto::Order>& get_all_open_orders() const override;
        /// @brief Close all channels and mark simulation complete.
        void  close() override;
    private:
        std::shared_ptr<QTrading::Log::Logger> logger;        ///< Logger for account/order/position events.
        std::vector<std::string>                    symbols_; ///< Stable symbol list.
        std::vector<MarketData>                     md_;      ///< CSV-backed data provider per symbol.
        std::vector<size_t>                         cursor_;  ///< Current read index per symbol.
        std::shared_ptr<Account>                    account;  ///< Simulated margin account engine.

        std::vector<dto::Position> last_pos_snapshot;   ///< Last-debounced snapshot of positions.
        std::vector<dto::Order>    last_ord_snapshot;   ///< Last-debounced snapshot of orders.

        // Multiway merge state: next timestamp for each symbol + a min-heap.
        struct HeapItem {
            uint64_t ts;
            size_t sym_id;
        };
        struct HeapItemGreater {
            bool operator()(const HeapItem& a, const HeapItem& b) const {
                if (a.ts != b.ts) return a.ts > b.ts;      // min-heap by timestamp
                return a.sym_id > b.sym_id;                // tie-break deterministically by symbol id
            }
        };

        std::vector<uint64_t> next_ts_by_symbol_;
        std::vector<uint8_t>  has_next_ts_;
        std::priority_queue<HeapItem, std::vector<HeapItem>, HeapItemGreater> next_ts_heap_;

        uint64_t last_account_version_{ 0 };
        uint64_t last_logged_version_{ static_cast<uint64_t>(-1) };

        // P1: Reusable per-step buffer to avoid rebuilding an unordered_map each tick.
        std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto> kline_snap_cache_;

        /// @brief Find the next timestamp to emit across all symbols.
        /// @param[out] ts Next timestamp (ms since epoch).
        /// @return True if data remains; false when all CSVs are exhausted.
        bool     next_timestamp(uint64_t& ts);
        /// @brief Build a MultiKlineDto for timestamp ts and advance cursors.
        /// @param ts Timestamp to snapshot.
        /// @param[out] out DTO to populate with per-symbol KlineDto or std::nullopt.
        void     build_multikline(uint64_t ts,
            QTrading::Dto::Market::Binance::MultiKlineDto& out);
        /// @brief Log current account balance, positions and orders via logger.
        inline void log_status();

        static bool vec_equal(const std::vector<dto::Position>& a,
            const std::vector<dto::Position>& b);
        static bool vec_equal(const std::vector<dto::Order>& a,
            const std::vector<dto::Order>& b);

        BinanceExchange(const BinanceExchange&) = delete;
        BinanceExchange& operator=(const BinanceExchange&) = delete;
    };

}
