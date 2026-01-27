#pragma once

#include <unordered_map>
#include <vector>
#include <optional>
#include <queue>
#include <cstdint>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include <memory_resource>

#include "Exchanges/IExchange.h"
#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include "Exchanges/BinanceSimulator/DataProvider/FundingRateData.hpp"
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Trading/Side.hpp"
#include "Logging/StepLogContext.hpp"
#include "Logging/AccountEventBuffer.hpp"
#include "Logging/OrderEventBuffer.hpp"
#include "Logging/PositionEventBuffer.hpp"
#include "Logging/MarketEventBuffer.hpp"
#include "Logging/FundingEventBuffer.hpp"
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
        struct SymbolDataset {
            std::string symbol;
            std::string kline_csv;
            std::optional<std::string> funding_csv;

            SymbolDataset(std::string sym, std::string kline, std::optional<std::string> funding)
                : symbol(std::move(sym)), kline_csv(std::move(kline)), funding_csv(std::move(funding)) {}
        };

        /// @brief Construct with CSV mappings, logger, initial balance and VIP level.
        /// @param symbolCsv Vector of (symbol, csv_file) pairs.
        /// @param logger Shared pointer to a Logger instance.
        /// @param init_balance Starting account balance.
        /// @param vip_level VIP level for fee tier (0–9).
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            std::shared_ptr<QTrading::Log::Logger> logger,
            double  init_balance = 1'000'000.0,
            int     vip_level = 0,
            uint64_t run_id = 0);

        /// @brief Construct with kline + optional funding CSV mappings.
        /// @param datasets Vector of (symbol, kline_csv, optional funding_csv).
        /// @param logger Shared pointer to a Logger instance.
        /// @param init_balance Starting account balance.
        /// @param vip_level VIP level for fee tier (0–9).
        BinanceExchange(const std::vector<SymbolDataset>& datasets,
            std::shared_ptr<QTrading::Log::Logger> logger,
            double  init_balance = 1'000'000.0,
            int     vip_level = 0,
            uint64_t run_id = 0);

        /// @brief Construct with CSV mappings, logger, and existing Account.
        /// @param symbolCsv Vector of (symbol, csv_file) pairs.
        /// @param logger Shared pointer to a Logger instance.
        /// @param account Shared pointer to a preconfigured Account.
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            std::shared_ptr<QTrading::Log::Logger> logger,
            std::shared_ptr<Account> account,
            uint64_t run_id = 0);

        /// @brief Construct with datasets, logger, and existing Account.
        /// @param datasets Vector of (symbol, kline_csv, optional funding_csv).
        /// @param logger Shared pointer to a Logger instance.
        /// @param account Shared pointer to a preconfigured Account.
        BinanceExchange(const std::vector<SymbolDataset>& datasets,
            std::shared_ptr<QTrading::Log::Logger> logger,
            std::shared_ptr<Account> account,
            uint64_t run_id = 0);
        ~BinanceExchange();

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
        /// @copydoc IExchange::set_symbol_leverage
        void set_symbol_leverage(const std::string& symbol, double new_leverage) override;
        /// @copydoc IExchange::get_symbol_leverage
        double get_symbol_leverage(const std::string& symbol) const override;
        /// @brief Close all channels and mark simulation complete.
        void  close() override;
        void  cancel_open_orders(const std::string& symbol) override;

        struct StatusSnapshot {
            struct PriceSnapshot {
                std::string symbol;
                double price{ 0.0 };
                bool has_price{ false };
            };

            uint64_t ts_exchange{ 0 };
            double wallet_balance{ 0.0 };
            double margin_balance{ 0.0 };
            double available_balance{ 0.0 };
            double unrealized_pnl{ 0.0 };
            double total_unrealized_pnl{ 0.0 };
            double progress_pct{ 0.0 };
            std::vector<PriceSnapshot> prices;
        };

        /// @brief Fill a lightweight snapshot for status reporting.
        void FillStatusSnapshot(StatusSnapshot& out) const;
    private:
        std::shared_ptr<QTrading::Log::Logger> logger;        ///< Logger for account/order/position events.
        QTrading::Log::Logger::ModuleId account_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId position_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId order_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId account_event_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId position_event_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId order_event_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId market_event_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        QTrading::Log::Logger::ModuleId funding_event_module_id_{ QTrading::Log::Logger::kInvalidModuleId };
        bool enable_event_logging_{ true };
        bool enable_klines_map_{ false };
        std::vector<std::string>                    symbols_; ///< Stable symbol list.
        std::shared_ptr<const std::vector<std::string>> symbols_shared_;
        std::vector<MarketData>                     md_;      ///< CSV-backed data provider per symbol.
        std::vector<size_t>                         cursor_;  ///< Current read index per symbol.
        std::vector<std::unique_ptr<FundingRateData>> funding_md_; ///< Optional funding data per symbol.
        std::vector<size_t>                         funding_cursor_; ///< Funding read index per symbol.
        std::vector<uint8_t>                        has_funding_;
        std::shared_ptr<Account>                    account;  ///< Simulated margin account engine.
        mutable std::mutex                          account_mtx_;

        std::vector<dto::Position> last_pos_snapshot;   ///< Last-debounced snapshot of positions.
        std::vector<dto::Order>    last_ord_snapshot;   ///< Last-debounced snapshot of orders.
        std::vector<dto::Position> last_event_pos_snapshot_;
        std::vector<dto::Order>    last_event_ord_snapshot_;
        std::optional<double>      last_wallet_balance_;
        uint64_t                   last_event_version_{ static_cast<uint64_t>(-1) };

        QTrading::Infra::Logging::StepLogContext log_ctx_{};
        std::pmr::unsynchronized_pool_resource log_event_pool_{ std::pmr::new_delete_resource() };
        QTrading::Infra::Logging::AccountEventBuffer account_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::PositionEventBuffer position_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::OrderEventBuffer order_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::MarketEventBuffer market_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::FundingEventBuffer funding_event_buffer_{ &log_event_pool_ };
        std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto> funding_events_scratch_;

        struct LogTask {
            QTrading::Infra::Logging::StepLogContext ctx;
            MultiKlinePtr market;
            std::vector<dto::Position> positions;
            std::vector<dto::Order> orders;
            std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto> funding_events;
            QTrading::Dto::Account::BalanceSnapshot balance;
            std::vector<Account::FillEvent> fill_events;
            uint64_t cur_ver{ 0 };
        };

        std::mutex log_mtx_;
        std::condition_variable log_cv_;
        std::deque<LogTask> log_queue_;
        std::thread log_thread_;
        std::atomic<bool> log_stop_{ false };

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
        uint64_t last_step_ts_{ 0 };

        // P1: Reusable per-step buffer to avoid rebuilding an unordered_map each tick.
        std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto> kline_snap_cache_;
        std::vector<size_t> kline_counts_;
        std::vector<double> last_close_by_symbol_;
        std::vector<uint8_t> has_last_close_;

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
        void log_status_snapshot(const QTrading::Dto::Account::BalanceSnapshot& balance,
            const std::vector<dto::Position>& positions,
            const std::vector<dto::Order>& orders,
            uint64_t cur_ver);
        /// @brief Log per-step market/account/order/position events with run/step/event sequencing.
        void log_events(QTrading::Infra::Logging::StepLogContext ctx,
            const QTrading::Dto::Market::Binance::MultiKlineDto& market,
            const std::vector<dto::Position>& cur_positions,
            const std::vector<dto::Order>& cur_orders,
            const std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>& funding_events,
            const QTrading::Dto::Account::BalanceSnapshot& balance,
            std::vector<Account::FillEvent>&& fill_events,
            uint64_t cur_ver);

        void start_log_thread_();
        void stop_log_thread_();
        void log_worker_();
        void enqueue_log_task_(LogTask&& task);

    public:
        void collect_funding_events(uint64_t ts,
            std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>& out);

        double progress_pct_() const;

        static bool vec_equal(const std::vector<dto::Position>& a,
            const std::vector<dto::Position>& b);
        static bool vec_equal(const std::vector<dto::Order>& a,
            const std::vector<dto::Order>& b);

        BinanceExchange(const BinanceExchange&) = delete;
        BinanceExchange& operator=(const BinanceExchange&) = delete;
    };

}
