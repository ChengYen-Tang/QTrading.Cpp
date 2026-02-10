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
#include <functional>
#include <thread>
#include <atomic>
#include <memory_resource>
#include <utility>

#include "Exchanges/IExchange.h"
#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include "Exchanges/BinanceSimulator/DataProvider/FundingRateData.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
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
            std::optional<QTrading::Dto::Trading::InstrumentType> instrument_type;

            SymbolDataset(std::string sym,
                std::string kline,
                std::optional<std::string> funding,
                std::optional<QTrading::Dto::Trading::InstrumentType> type = std::nullopt)
                : symbol(std::move(sym)),
                kline_csv(std::move(kline)),
                funding_csv(std::move(funding)),
                instrument_type(type) {}
        };

        class SpotApi {
        public:
            explicit SpotApi(BinanceExchange& owner) : owner_(owner) {}

            bool place_order(const std::string& symbol,
                double quantity,
                double price,
                QTrading::Dto::Trading::OrderSide side,
                bool reduce_only = false,
                const std::string& client_order_id = {},
                Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);

            bool place_order(const std::string& symbol,
                double quantity,
                QTrading::Dto::Trading::OrderSide side,
                bool reduce_only = false,
                const std::string& client_order_id = {},
                Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);

            void close_position(const std::string& symbol, double price = 0.0);
            void cancel_open_orders(const std::string& symbol);

        private:
            BinanceExchange& owner_;
        };

        class PerpApi {
        public:
            explicit PerpApi(BinanceExchange& owner) : owner_(owner) {}

            bool place_order(const std::string& symbol,
                double quantity,
                double price,
                QTrading::Dto::Trading::OrderSide side,
                QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
                bool reduce_only = false,
                const std::string& client_order_id = {},
                Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);

            bool place_order(const std::string& symbol,
                double quantity,
                QTrading::Dto::Trading::OrderSide side,
                QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
                bool reduce_only = false,
                const std::string& client_order_id = {},
                Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);

            void close_position(const std::string& symbol, double price = 0.0);
            void close_position(const std::string& symbol,
                QTrading::Dto::Trading::PositionSide position_side,
                double price = 0.0);
            void cancel_open_orders(const std::string& symbol);
            void set_symbol_leverage(const std::string& symbol, double new_leverage);
            double get_symbol_leverage(const std::string& symbol) const;

        private:
            BinanceExchange& owner_;
        };

        class AccountApi {
        public:
            explicit AccountApi(BinanceExchange& owner) : owner_(owner) {}

            QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const;
            QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const;
            double get_total_cash_balance() const;
            bool transfer_spot_to_perp(double amount);
            bool transfer_perp_to_spot(double amount);

        private:
            BinanceExchange& owner_;
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
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            std::shared_ptr<QTrading::Log::Logger> logger,
            const Account::AccountInitConfig& account_init,
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
        BinanceExchange(const std::vector<SymbolDataset>& datasets,
            std::shared_ptr<QTrading::Log::Logger> logger,
            const Account::AccountInitConfig& account_init,
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

        SpotApi spot;
        PerpApi perp;
        AccountApi account;

        /// @copydoc IExchange::step
        bool  step() override;

        /// @copydoc IExchange::get_all_positions
        const std::vector<dto::Position>& get_all_positions()   const override;
        /// @copydoc IExchange::get_all_open_orders
        const std::vector<dto::Order>& get_all_open_orders() const override;
        /// @copydoc IExchange::set_symbol_leverage
        /// @note Legacy top-level API kept for compatibility.
        ///       New code should prefer `exchange.perp.set_symbol_leverage(...)`.
        void set_symbol_leverage(const std::string& symbol, double new_leverage) override;
        /// @copydoc IExchange::get_symbol_leverage
        /// @note Legacy top-level API kept for compatibility.
        ///       New code should prefer `exchange.perp.get_symbol_leverage(...)`.
        double get_symbol_leverage(const std::string& symbol) const override;
        QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const;
        QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const;
        double get_total_cash_balance() const;
        bool transfer_spot_to_perp(double amount);
        bool transfer_perp_to_spot(double amount);
        void set_order_latency_bars(size_t bars);
        size_t order_latency_bars() const;
        struct AsyncOrderAck {
            enum class Status {
                Pending = 0,
                Accepted = 1,
                Rejected = 2,
            };

            uint64_t request_id{ 0 };
            Status status{ Status::Pending };
            QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
            std::string symbol;
            double quantity{ 0.0 };
            double price{ 0.0 }; // <=0 indicates market-style request
            QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
            QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
            bool reduce_only{ false };
            uint64_t submitted_step{ 0 };
            uint64_t due_step{ 0 };
            uint64_t resolved_step{ 0 }; // 0 until final Accepted/Rejected
            Account::OrderRejectInfo::Code reject_code{ Account::OrderRejectInfo::Code::None };
            std::string reject_message;
            std::string client_order_id;
            Account::SelfTradePreventionMode stp_mode{ Account::SelfTradePreventionMode::None };
            int binance_error_code{ 0 };
            std::string binance_error_message;
        };
        std::vector<AsyncOrderAck> drain_async_order_acks();
        enum class FundingApplyTiming {
            BeforeMatching = 0,
            AfterMatching = 1,
        };
        void set_funding_apply_timing(FundingApplyTiming timing);
        FundingApplyTiming funding_apply_timing() const;
        // Max allowed age (ms) for using last close as funding mark-price fallback.
        // 0 means no limit.
        void set_funding_mark_price_max_age_ms(uint64_t max_age_ms);
        uint64_t funding_mark_price_max_age_ms() const;
        void set_uncertainty_band_bps(double bps);
        double uncertainty_band_bps() const;
        /// @brief Close all channels and mark simulation complete.
        void  close() override;

        struct StatusSnapshot {
            struct PriceSnapshot {
                std::string symbol;
                double price{ 0.0 };
                bool has_price{ false };
            };

            uint64_t ts_exchange{ 0 };
            // Legacy perp-oriented fields kept for compatibility.
            double wallet_balance{ 0.0 };
            double margin_balance{ 0.0 };
            double available_balance{ 0.0 };
            double unrealized_pnl{ 0.0 };
            double total_unrealized_pnl{ 0.0 };

            // Dual-ledger status fields.
            double perp_wallet_balance{ 0.0 };
            double perp_margin_balance{ 0.0 };
            double perp_available_balance{ 0.0 };
            double spot_cash_balance{ 0.0 };
            double spot_available_balance{ 0.0 };
            double spot_inventory_value{ 0.0 };
            double spot_ledger_value{ 0.0 };
            double total_cash_balance{ 0.0 };
            double total_ledger_value{ 0.0 };
            double total_ledger_value_base{ 0.0 };
            double total_ledger_value_conservative{ 0.0 };
            double total_ledger_value_optimistic{ 0.0 };
            double uncertainty_band_bps{ 0.0 };

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
        std::vector<double>                         last_funding_rate_by_symbol_;
        std::vector<uint64_t>                       last_funding_time_by_symbol_;
        std::vector<uint8_t>                        has_last_funding_;
        std::shared_ptr<Account>                    account_engine_;  ///< Simulated margin account engine.
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
            QTrading::Dto::Account::BalanceSnapshot perp_balance;
            QTrading::Dto::Account::BalanceSnapshot spot_balance;
            double total_cash_balance{ 0.0 };
            double spot_inventory_value{ 0.0 };
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
        std::vector<uint64_t> last_close_ts_by_symbol_;
        std::vector<uint8_t> has_last_close_;
        size_t order_latency_bars_{ 0 };
        FundingApplyTiming funding_apply_timing_{ FundingApplyTiming::BeforeMatching };
        uint64_t funding_mark_price_max_age_ms_{ 0 };
        uint64_t processed_steps_{ 0 };
        double uncertainty_band_bps_{ 0.0 };
        struct DeferredOrderCommand {
            uint64_t due_step{ 0 };
            std::function<void(Account&, uint64_t)> fn{};
        };
        std::deque<DeferredOrderCommand> deferred_order_commands_;
        std::vector<AsyncOrderAck> async_order_acks_{};
        uint64_t next_async_order_request_id_{ 1 };

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
        void log_status_snapshot(const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
            const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
            double total_cash_balance,
            double spot_inventory_value,
            const std::vector<dto::Position>& positions,
            const std::vector<dto::Order>& orders,
            uint64_t cur_ver);
        /// @brief Log per-step market/account/order/position events with run/step/event sequencing.
        void log_events(QTrading::Infra::Logging::StepLogContext ctx,
            const QTrading::Dto::Market::Binance::MultiKlineDto& market,
            const std::vector<dto::Position>& cur_positions,
            const std::vector<dto::Order>& cur_orders,
            const std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>& funding_events,
            const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
            const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
            double total_cash_balance,
            double spot_inventory_value,
            std::vector<Account::FillEvent>&& fill_events,
            uint64_t cur_ver);

        void start_log_thread_();
        void stop_log_thread_();
        void log_worker_();
        void enqueue_log_task_(LogTask&& task);
        void enqueue_deferred_order_locked_(uint64_t due_step, std::function<void(Account&, uint64_t)> fn);
        void flush_deferred_orders_locked_(uint64_t step_seq);
        void push_async_order_ack_locked_(AsyncOrderAck ack);
        static std::pair<int, std::string> map_binance_reject_(const std::optional<Account::OrderRejectInfo>& reject);
        bool interpolate_mark_price_(size_t sym_id, uint64_t ts, double& out_price) const;

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
        BinanceExchange(BinanceExchange&&) = delete;
        BinanceExchange& operator=(BinanceExchange&&) = delete;
    };

}
