#pragma once

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
#include <array>

#include "Exchanges/IExchange.h"
#include "Data/Binance/MarketData.hpp"
#include "Data/Binance/FundingRateData.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountCoreV2.hpp"
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
            std::optional<std::string> mark_kline_csv;
            std::optional<std::string> index_kline_csv;
            std::optional<QTrading::Dto::Trading::InstrumentType> instrument_type;

            SymbolDataset(std::string sym,
                std::string kline,
                std::optional<std::string> funding,
                std::optional<std::string> mark_kline = std::nullopt,
                std::optional<std::string> index_kline = std::nullopt,
                std::optional<QTrading::Dto::Trading::InstrumentType> type = std::nullopt)
                : symbol(std::move(sym)),
                kline_csv(std::move(kline)),
                funding_csv(std::move(funding)),
                mark_kline_csv(std::move(mark_kline)),
                index_kline_csv(std::move(index_kline)),
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
            bool place_market_order_quote(const std::string& symbol,
                double quote_order_qty,
                QTrading::Dto::Trading::OrderSide side = QTrading::Dto::Trading::OrderSide::Buy,
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
            bool place_close_position_order(const std::string& symbol,
                QTrading::Dto::Trading::OrderSide side,
                QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
                double price = 0.0,
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

        /// @brief Construct with trade/mark/index kline + optional funding CSV mappings.
        /// @param datasets Vector of per-symbol datasets.
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
        /// @param datasets Vector of per-symbol datasets.
        /// @param logger Shared pointer to a Logger instance.
        /// @param account Shared pointer to a preconfigured Account.
        BinanceExchange(const std::vector<SymbolDataset>& datasets,
            std::shared_ptr<QTrading::Log::Logger> logger,
            std::shared_ptr<Account> account,
            uint64_t run_id = 0);
        BinanceExchange(const std::vector<SymbolDataset>& datasets,
            std::shared_ptr<QTrading::Log::Logger> logger,
            std::shared_ptr<Account> account,
            std::shared_ptr<AccountCoreV2> account_core_v2,
            uint64_t run_id);
        ~BinanceExchange();

        SpotApi spot;
        PerpApi perp;
        AccountApi account;

        enum class CoreMode : uint8_t {
            LegacyOnly = 0,
            NewCoreShadow = 1,
            NewCorePrimary = 2,
        };

        struct CoreModePolicy final {
            static constexpr CoreMode normalize(CoreMode mode) noexcept
            {
                switch (mode) {
                case CoreMode::LegacyOnly:
                case CoreMode::NewCoreShadow:
                case CoreMode::NewCorePrimary:
                    return mode;
                default:
                    return CoreMode::LegacyOnly;
                }
            }

            static constexpr bool is_legacy_only(CoreMode mode) noexcept
            {
                return normalize(mode) == CoreMode::LegacyOnly;
            }

            static constexpr bool is_shadow_compare(CoreMode mode) noexcept
            {
                return normalize(mode) == CoreMode::NewCoreShadow;
            }

            static constexpr bool is_new_core_primary(CoreMode mode) noexcept
            {
                return normalize(mode) == CoreMode::NewCorePrimary;
            }

            static constexpr bool allows_compare_diagnostics(CoreMode mode) noexcept
            {
                return !is_legacy_only(mode);
            }

            static constexpr const char* name(CoreMode mode) noexcept
            {
                switch (normalize(mode)) {
                case CoreMode::LegacyOnly:
                    return "LegacyOnly";
                case CoreMode::NewCoreShadow:
                    return "NewCoreShadow";
                case CoreMode::NewCorePrimary:
                    return "NewCorePrimary";
                default:
                    return "LegacyOnly";
                }
            }
        };

        struct RunStepResult {
            bool progressed{ false };
            bool fallback_to_legacy{ false };
            bool compare_snapshot_ready{ false };
        };

        struct StepCompareSnapshot {
            uint64_t ts_exchange{ 0 };
            uint64_t step_seq{ 0 };
            bool progressed{ false };
            uint64_t position_count{ 0 };
            uint64_t open_order_count{ 0 };
            double perp_wallet_balance{ 0.0 };
            double spot_wallet_balance{ 0.0 };
            double total_cash_balance{ 0.0 };
        };

        struct StepCompareDiagnostic {
            CoreMode mode{ CoreMode::LegacyOnly };
            bool compared{ false };
            bool matched{ false };
            std::string reason;
            StepCompareSnapshot legacy{};
            StepCompareSnapshot candidate{};
        };

        struct AccountFacadeBridgeDiagnostic {
            CoreMode mode{ CoreMode::LegacyOnly };
            bool has_v2{ false };
            bool routed_to_v2{ false };
            bool production_default_legacy_only{ false };
            std::string reason;
        };

        struct SessionReplayCoexistenceDiagnostic {
            CoreMode requested_mode{ CoreMode::LegacyOnly };
            CoreMode effective_mode{ CoreMode::LegacyOnly };
            bool production_default_legacy_only{ false };
            bool force_legacy_only{ false };
            bool shadow_compare_enabled{ false };
            bool v2_explicit_enabled{ false };
            bool compare_artifact_enabled{ false };
            bool fallback_to_legacy{ false };
            bool fail_close_protected{ true };
            std::string reason;
        };

        enum class ReplayStepKind : uint8_t {
            EndOfStream = 0,
            MarketOnly = 1,
            FundingOnly = 2,
            Mixed = 3,
        };

        struct ReplayFrameV2Diagnostic {
            bool has_next{ false };
            bool end_of_stream{ false };
            uint64_t ts_exchange{ 0 };
            uint64_t next_kline_ts{ 0 };
            uint64_t next_funding_ts{ 0 };
            ReplayStepKind step_kind{ ReplayStepKind::EndOfStream };
            uint32_t symbols_with_trade{ 0 };
            uint32_t symbols_with_funding{ 0 };
            std::string reason;
        };

        struct TradingSessionCoreV2Diagnostic {
            uint64_t step_seq{ 0 };
            uint64_t ts_exchange{ 0 };
            bool progressed{ false };
            bool terminated{ false };
            bool terminated_end_of_stream{ false };
            bool terminated_balance_depleted{ false };
            bool funding_before_matching{ true };
            uint64_t deferred_due_step{ 0 };
            uint32_t deferred_executed{ 0 };
            uint64_t async_ack_queue_size{ 0 };
            std::string reason;
        };

        struct BinanceExchangeFacadeBridgeDiagnostic {
            CoreMode requested_mode{ CoreMode::LegacyOnly };
            CoreMode effective_mode{ CoreMode::LegacyOnly };
            bool progressed{ false };
            bool fallback_to_legacy{ false };
            bool used_v2_session_core{ false };
            bool preserve_capture_boundary{ true };
            bool preserve_log_batch_boundary{ true };
            bool preserve_timestamp_semantics{ true };
            uint64_t step_seq{ 0 };
            uint64_t ts_exchange{ 0 };
            std::string reason;
        };

        enum class EventPublishMode : uint8_t {
            LegacyDirect = 0,
            DomainEventAdapter = 1,
            DualPublishCompare = 2,
        };

        struct EventPublishCompareSnapshot {
            uint64_t run_id{ 0 };
            uint64_t step_seq{ 0 };
            uint64_t ts_exchange{ 0 };
            uint64_t market_event_count{ 0 };
            uint64_t funding_event_count{ 0 };
            uint64_t fill_event_count{ 0 };
            uint64_t account_event_count{ 0 };
            uint64_t position_event_count{ 0 };
            uint64_t order_event_count{ 0 };
            uint64_t account_snapshot_row_count{ 0 };
            uint64_t position_snapshot_row_count{ 0 };
            uint64_t order_snapshot_row_count{ 0 };
            int32_t account_module_id{ -1 };
            int32_t position_module_id{ -1 };
            int32_t order_module_id{ -1 };
            int32_t account_event_module_id{ -1 };
            int32_t position_event_module_id{ -1 };
            int32_t order_event_module_id{ -1 };
            int32_t market_event_module_id{ -1 };
            int32_t funding_event_module_id{ -1 };
            std::array<int32_t, 8> module_order{};
            uint64_t payload_fingerprint{ 0 };
            bool has_position_snapshot{ false };
            bool has_order_snapshot{ false };
        };

        struct EventPublishCompareDiagnostic {
            EventPublishMode mode{ EventPublishMode::LegacyDirect };
            bool compared{ false };
            bool matched{ false };
            std::string reason;
            EventPublishCompareSnapshot legacy{};
            EventPublishCompareSnapshot candidate{};
        };

        /// @copydoc IExchange::step
        bool  step() override;
        void set_core_mode(CoreMode mode);
        CoreMode core_mode() const;
        void set_force_legacy_only(bool enabled);
        bool force_legacy_only() const;
        std::optional<SessionReplayCoexistenceDiagnostic> consume_last_session_replay_coexistence_diagnostic();
        std::optional<ReplayFrameV2Diagnostic> consume_last_replay_frame_v2_diagnostic();
        std::optional<TradingSessionCoreV2Diagnostic> consume_last_trading_session_core_v2_diagnostic();
        std::optional<BinanceExchangeFacadeBridgeDiagnostic> consume_last_binance_exchange_facade_bridge_diagnostic();
        std::optional<StepCompareDiagnostic> consume_last_compare_diagnostic();
        std::optional<AccountFacadeBridgeDiagnostic> consume_last_account_facade_bridge_diagnostic();
        void set_event_publish_mode(EventPublishMode mode);
        EventPublishMode event_publish_mode() const;
        std::optional<EventPublishCompareDiagnostic> consume_last_event_publish_diagnostic();

        struct SideEffectStepSnapshot {
            uint64_t run_id{ 0 };
            uint64_t step_seq{ 0 };
            uint64_t ts_exchange{ 0 };
            uint64_t state_version{ 0 };
            bool market_published{ false };
            bool position_published{ false };
            bool order_published{ false };
        };

        struct SideEffectAdapterConfig {
            std::function<void(MultiKlinePtr)> market_publisher{};
            std::function<void(const std::vector<dto::Position>&)> position_publisher{};
            std::function<void(const std::vector<dto::Order>&)> order_publisher{};
            std::function<void(const SideEffectStepSnapshot&)> external_hook{};
        };

        void set_side_effect_adapters(SideEffectAdapterConfig config);
        void reset_side_effect_adapters();

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
            bool close_position{ false };
        };
        std::vector<AsyncOrderAck> drain_async_order_acks();
        enum class FundingApplyTiming {
            BeforeMatching = 0,
            AfterMatching = 1,
        };
        struct ReferenceFundingResolverDiagnostic {
            uint64_t ts_exchange{ 0 };
            uint32_t funding_rows_seen{ 0 };
            uint32_t funding_rows_applied{ 0 };
            uint32_t funding_rows_skipped_no_mark{ 0 };
            uint32_t funding_rows_skipped_duplicate{ 0 };
            uint32_t mark_source_raw_count{ 0 };
            uint32_t mark_source_interpolated_count{ 0 };
            FundingApplyTiming funding_apply_timing{ FundingApplyTiming::BeforeMatching };
            std::string reason;
        };
        enum class ReferencePriceSource : int32_t {
            None = 0,
            Raw = 1,
            Interpolated = 2,
        };
        void set_funding_apply_timing(FundingApplyTiming timing);
        FundingApplyTiming funding_apply_timing() const;
        std::optional<ReferenceFundingResolverDiagnostic> consume_last_reference_funding_resolver_diagnostic();
        void set_uncertainty_band_bps(double bps);
        double uncertainty_band_bps() const;
        void set_mark_index_basis_thresholds_bps(double warning_bps, double stress_bps);
        void set_basis_risk_leverage_caps(double warning_cap, double stress_cap);
        void set_simulator_risk_overlay_enabled(bool enabled);
        bool simulator_risk_overlay_enabled() const;
        void set_basis_risk_guard_enabled(bool enabled);
        bool basis_risk_guard_enabled() const;
        void set_basis_stress_blocks_opening_orders(bool enabled);
        bool basis_stress_blocks_opening_orders() const;
        /// @brief Close all channels and mark simulation complete.
        void  close() override;

        struct StatusSnapshot {
            struct PriceSnapshot {
                std::string symbol;
                // Legacy trade close fields kept for compatibility.
                double price{ 0.0 };
                bool has_price{ false };
                // Explicit split prices.
                double trade_price{ 0.0 };
                bool has_trade_price{ false };
                double mark_price{ 0.0 };
                bool has_mark_price{ false };
                int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
                double index_price{ 0.0 };
                bool has_index_price{ false };
                int32_t index_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
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
            uint32_t basis_warning_symbols{ 0 };
            uint32_t basis_stress_symbols{ 0 };
            uint64_t basis_stress_blocked_orders{ 0 };
            uint64_t funding_applied_events{ 0 };
            uint64_t funding_skipped_no_mark{ 0 };

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
        std::vector<std::string>                    symbols_; ///< Stable symbol list.
        std::shared_ptr<const std::vector<std::string>> symbols_shared_;
        std::vector<MarketData>                     md_;      ///< CSV-backed data provider per symbol.
        std::vector<std::unique_ptr<MarketData>>   mark_md_; ///< Optional mark-price kline data per symbol.
        std::vector<std::unique_ptr<MarketData>>   index_md_; ///< Optional index-price kline data per symbol.
        std::vector<uint8_t>                       has_mark_md_;
        std::vector<uint8_t>                       has_index_md_;
        std::vector<size_t>                         cursor_;  ///< Current read index per symbol.
        std::vector<size_t>                         kline_window_begin_idx_; ///< Inclusive begin index per symbol.
        std::vector<size_t>                         kline_window_end_idx_;   ///< Exclusive end index per symbol.
        std::vector<std::unique_ptr<FundingRateData>> funding_md_; ///< Optional funding data per symbol.
        std::vector<size_t>                         funding_cursor_; ///< Funding read index per symbol.
        std::vector<size_t>                         funding_window_end_idx_; ///< Exclusive funding end index per symbol.
        std::vector<uint8_t>                        has_funding_;
        std::vector<double>                         last_funding_rate_by_symbol_;
        std::vector<uint64_t>                       last_funding_time_by_symbol_;
        std::vector<uint8_t>                        has_last_funding_;
        std::vector<uint64_t>                       last_applied_funding_time_by_symbol_;
        std::vector<double>                         last_applied_funding_rate_by_symbol_;
        std::vector<uint8_t>                        has_last_applied_funding_;
        std::optional<uint64_t>                     replay_start_ts_ms_; ///< Optional replay window start (inclusive).
        std::optional<uint64_t>                     replay_end_ts_ms_;   ///< Optional replay window end (inclusive).
        std::shared_ptr<Account>                    account_engine_;  ///< Simulated margin account engine.
        std::shared_ptr<Account>                    legacy_account_engine_;
        std::shared_ptr<AccountCoreV2>              account_core_v2_;
        mutable std::mutex                          account_mtx_;
        mutable std::mutex                          state_mtx_;
        using PositionSnapshotPtr = std::shared_ptr<const std::vector<dto::Position>>;
        using OrderSnapshotPtr = std::shared_ptr<const std::vector<dto::Order>>;

        PositionSnapshotPtr last_pos_snapshot_;   ///< Last-debounced snapshot of positions.
        OrderSnapshotPtr    last_ord_snapshot_;   ///< Last-debounced snapshot of orders.
        PositionSnapshotPtr last_event_pos_snapshot_;
        OrderSnapshotPtr    last_event_ord_snapshot_;
        std::optional<double>      last_wallet_balance_;
        uint64_t                   last_event_version_{ static_cast<uint64_t>(-1) };

        QTrading::Infra::Logging::StepLogContext log_ctx_{};
        std::pmr::unsynchronized_pool_resource log_event_pool_{ std::pmr::new_delete_resource() };
        QTrading::Infra::Logging::AccountEventBuffer account_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::PositionEventBuffer position_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::OrderEventBuffer order_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::MarketEventBuffer market_event_buffer_{ &log_event_pool_ };
        QTrading::Infra::Logging::FundingEventBuffer funding_event_buffer_{ &log_event_pool_ };
        Account::MarketUpdateById market_update_by_id_{};

        enum class DomainEventKind : uint8_t {
            Market = 0,
            Funding = 1,
            Account = 2,
            Position = 3,
            Order = 4,
            AccountSnapshot = 5,
            PositionSnapshot = 6,
            OrderSnapshot = 7,
        };

        struct MarketDomainEvent {
            std::string symbol;
            bool has_kline{ false };
            QTrading::Dto::Market::Binance::TradeKlineDto kline{};
            bool has_mark_price{ false };
            double mark_price{ 0.0 };
            int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
            bool has_index_price{ false };
            double index_price{ 0.0 };
            int32_t index_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
        };
        using DomainMarketEvent = MarketDomainEvent;

        struct FundingDomainEvent {
            uint64_t run_id{ 0 };
            uint64_t step_seq{ 0 };
            uint64_t event_seq{ 0 };
            uint64_t ts_local{ 0 };

            std::string symbol;
            int32_t instrument_type{ -1 };
            uint64_t funding_time{ 0 };
            double rate{ 0.0 };
            bool has_mark_price{ false };
            double mark_price{ 0.0 };
            int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
            int32_t skip_reason{ 0 };
            int64_t position_id{ 0 };
            bool is_long{ false };
            double quantity{ 0.0 };
            double funding{ 0.0 };
        };
        using DomainFundingEvent = FundingDomainEvent;

        using DomainFillEvent = Account::FillEvent;
        struct AccountDomainEvent {
            DomainFillEvent fill{};
        };
        struct PositionDomainEvent {
            DomainFillEvent fill{};
        };
        struct OrderDomainEvent {
            DomainFillEvent fill{};
        };
        struct AccountSnapshotEvent {
            QTrading::Dto::Account::BalanceSnapshot perp_balance{};
            QTrading::Dto::Account::BalanceSnapshot spot_balance{};
            double total_cash_balance{ 0.0 };
            double spot_inventory_value{ 0.0 };
        };
        struct PositionSnapshotEvent {
            PositionSnapshotPtr positions;
        };
        struct OrderSnapshotEvent {
            OrderSnapshotPtr orders;
        };

        struct EventEnvelope {
            QTrading::Infra::Logging::StepLogContext ctx;
            std::vector<MarketDomainEvent> market_events;
            std::vector<FundingDomainEvent> funding_events;
            std::vector<AccountDomainEvent> account_events;
            std::vector<PositionDomainEvent> position_events;
            std::vector<OrderDomainEvent> order_events;
            AccountSnapshotEvent account_snapshot{};
            PositionSnapshotEvent position_snapshot{};
            OrderSnapshotEvent order_snapshot{};
            std::vector<DomainFillEvent> fill_events;
            uint64_t cur_ver{ 0 };

            static constexpr std::array<DomainEventKind, 8> kKinds{
                DomainEventKind::Market,
                DomainEventKind::Funding,
                DomainEventKind::Account,
                DomainEventKind::Position,
                DomainEventKind::Order,
                DomainEventKind::AccountSnapshot,
                DomainEventKind::PositionSnapshot,
                DomainEventKind::OrderSnapshot,
            };
        };

        class IEventPublisher;
        class LegacyAccountFacadeBridge;
        class LegacyLogAdapter;
        class LegacyEventPublisher;
        class AsyncEventPublisher;
        class NullEventPublisher;
        class EventCaptureBoundary;
        class MarketReplayEngineV2;
        class MarketReplayEngine;
        class ReferencePriceResolver;
        class FundingTimelineResolver;
        class TradingSessionCoreV2;
        class BinanceExchangeFacadeBridge;
        class StatusSnapshotBuilder;
        class SimulatorRiskOverlayEngine;
        class CoreDispatchFacade;
        class LegacyCoreSessionAdapter;
        class NewCoreSessionAdapter;
        class ExchangeSession;
        std::unique_ptr<IEventPublisher> event_publisher_;
        std::unique_ptr<LegacyAccountFacadeBridge> account_facade_bridge_;

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
        std::atomic<CoreMode> core_mode_{ CoreMode::NewCorePrimary };
        mutable std::mutex compare_diag_mtx_;
        std::optional<StepCompareDiagnostic> last_compare_diagnostic_;
        mutable std::mutex account_facade_bridge_diag_mtx_;
        std::optional<AccountFacadeBridgeDiagnostic> last_account_facade_bridge_diagnostic_;
        std::atomic<bool> force_legacy_only_{ false };
        mutable std::mutex session_replay_diag_mtx_;
        std::optional<SessionReplayCoexistenceDiagnostic> last_session_replay_coexistence_diagnostic_;
        mutable std::mutex replay_frame_v2_diag_mtx_;
        std::optional<ReplayFrameV2Diagnostic> last_replay_frame_v2_diagnostic_;
        mutable std::mutex trading_session_core_v2_diag_mtx_;
        std::optional<TradingSessionCoreV2Diagnostic> last_trading_session_core_v2_diagnostic_;
        mutable std::mutex binance_exchange_facade_bridge_diag_mtx_;
        std::optional<BinanceExchangeFacadeBridgeDiagnostic> last_binance_exchange_facade_bridge_diagnostic_;
        mutable std::mutex reference_funding_diag_mtx_;
        std::optional<ReferenceFundingResolverDiagnostic> last_reference_funding_resolver_diagnostic_;
        std::atomic<EventPublishMode> event_publish_mode_{ EventPublishMode::LegacyDirect };
        mutable std::mutex event_publish_diag_mtx_;
        std::optional<EventPublishCompareDiagnostic> last_event_publish_diagnostic_;
        std::function<void(MultiKlinePtr)> market_channel_publisher_{};
        std::function<void(const std::vector<dto::Position>&)> position_channel_publisher_{};
        std::function<void(const std::vector<dto::Order>&)> order_channel_publisher_{};
        std::function<void(const SideEffectStepSnapshot&)> external_side_effect_hook_{};

        // P2: Reusable per-step market/reference caches.
        std::vector<size_t> kline_counts_;
        std::vector<double> last_close_by_symbol_;
        std::vector<uint64_t> last_close_ts_by_symbol_;
        std::vector<uint8_t> has_last_close_;
        std::vector<double> last_mark_by_symbol_;
        std::vector<uint64_t> last_mark_ts_by_symbol_;
        std::vector<uint8_t> has_last_mark_;
        std::vector<int32_t> last_mark_source_by_symbol_;
        std::vector<double> last_index_by_symbol_;
        std::vector<uint64_t> last_index_ts_by_symbol_;
        std::vector<uint8_t> has_last_index_;
        std::vector<int32_t> last_index_source_by_symbol_;
        std::vector<double> last_mark_index_basis_bps_by_symbol_;
        std::vector<uint8_t> has_last_mark_index_basis_;
        double last_mark_index_max_abs_basis_bps_{ 0.0 };
        struct SimulatorRiskOverlay {
            bool enabled{ true };
            bool basis_stress_blocks_opening_orders{ true };
            double mark_index_warning_bps{ 50.0 };
            double mark_index_stress_bps{ 150.0 };
            double basis_warning_leverage_cap{ 10.0 };
            double basis_stress_leverage_cap{ 5.0 };
            uint32_t warning_symbols{ 0 };
            uint32_t stress_symbols{ 0 };
            std::vector<uint8_t> warning_active_by_symbol{};
            std::vector<uint8_t> stress_active_by_symbol{};
            std::atomic<uint64_t> stress_blocked_orders{ 0 };
        };
        SimulatorRiskOverlay simulator_risk_overlay_{};
        uint64_t funding_applied_events_total_{ 0 };
        uint64_t funding_skipped_no_mark_total_{ 0 };
        size_t order_latency_bars_{ 0 };
        FundingApplyTiming funding_apply_timing_{ FundingApplyTiming::BeforeMatching };
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
        /// @param[out] out DTO to populate with per-symbol TradeKlineDto or std::nullopt.
        void     build_multikline(uint64_t ts,
            QTrading::Dto::Market::Binance::MultiKlineDto& out);
        /// @brief Log current account balance, positions and orders via logger.
        void log_status_snapshot(const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
            const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
            double total_cash_balance,
            double spot_inventory_value,
            const PositionSnapshotPtr& positions,
            const OrderSnapshotPtr& orders,
            uint64_t cur_ver);
        /// @brief Log per-step market/account/order/position events with run/step/event sequencing.
        void log_events(QTrading::Infra::Logging::StepLogContext ctx,
            const std::vector<DomainMarketEvent>& market_events,
            const PositionSnapshotPtr& cur_positions,
            const OrderSnapshotPtr& cur_orders,
            const std::vector<DomainFundingEvent>& funding_events,
            const QTrading::Dto::Account::BalanceSnapshot& perp_balance,
            const QTrading::Dto::Account::BalanceSnapshot& spot_balance,
            double total_cash_balance,
            double spot_inventory_value,
            std::vector<DomainFillEvent>&& fill_events,
            uint64_t cur_ver);
        bool run_step_session_();
        RunStepResult dispatch_step_(CoreMode mode);
        StepCompareSnapshot build_step_compare_snapshot_(const RunStepResult& result) const;
        std::optional<std::string> compare_step_snapshots_(
            const StepCompareSnapshot& legacy_snapshot,
            const StepCompareSnapshot& candidate_snapshot) const;
        void record_compare_diagnostic_(StepCompareDiagnostic diag);
        void record_account_facade_bridge_diagnostic_(AccountFacadeBridgeDiagnostic diag);
        void record_session_replay_coexistence_diagnostic_(SessionReplayCoexistenceDiagnostic diag);
        void record_replay_frame_v2_diagnostic_(ReplayFrameV2Diagnostic diag);
        void record_trading_session_core_v2_diagnostic_(TradingSessionCoreV2Diagnostic diag);
        void record_binance_exchange_facade_bridge_diagnostic_(BinanceExchangeFacadeBridgeDiagnostic diag);
        void record_reference_funding_resolver_diagnostic_(ReferenceFundingResolverDiagnostic diag);
        EventPublishCompareSnapshot build_event_publish_compare_snapshot_(
            const EventEnvelope& envelope) const;
        std::optional<std::string> compare_event_publish_snapshots_(
            const EventPublishCompareSnapshot& legacy_snapshot,
            const EventPublishCompareSnapshot& candidate_snapshot) const;
        void record_event_publish_diagnostic_(EventPublishCompareDiagnostic diag);
        void emit_legacy_rows_from_event_envelope_(EventEnvelope&& envelope);
        void install_default_side_effect_adapters_();

        void publish_log_task_(EventEnvelope&& task);
        void enqueue_deferred_order_locked_(uint64_t due_step, std::function<void(Account&, uint64_t)> fn);
        size_t flush_deferred_orders_with_count_locked_(uint64_t step_seq);
        void flush_deferred_orders_locked_(uint64_t step_seq);
        void push_async_order_ack_locked_(AsyncOrderAck ack);
        static std::pair<int, std::string> map_binance_reject_(const std::optional<Account::OrderRejectInfo>& reject);
        bool interpolate_mark_price_(size_t sym_id, uint64_t ts, double& out_price) const;
        bool interpolate_index_price_(size_t sym_id, uint64_t ts, double& out_price) const;
        bool resolve_mark_price_with_source_(size_t sym_id,
            uint64_t ts,
            double& out_price,
            ReferencePriceSource& out_source) const;
        bool resolve_index_price_with_source_(size_t sym_id,
            uint64_t ts,
            double& out_price,
            ReferencePriceSource& out_source) const;
        bool perp_opening_blocked_by_basis_stress_account_locked_(
            Account& acc,
            const std::string& symbol,
            QTrading::Dto::Trading::OrderSide side,
            QTrading::Dto::Trading::PositionSide position_side,
            bool reduce_only) const;
        void collect_funding_events_unlocked_(uint64_t ts,
            std::vector<DomainFundingEvent>& out);
        double progress_pct_unlocked_() const;

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
