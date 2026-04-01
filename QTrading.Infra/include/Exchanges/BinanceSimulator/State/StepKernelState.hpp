#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "Data/Binance/MarketData.hpp"
#include "Data/Binance/FundingRateData.hpp"
#include "Dto/Market/Binance/FundingRate.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeDiagnostics.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelHeapTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Mutable state owned by StepKernel/MarketReplayKernel.
/// This struct is the replay hot-path state; keeping it compact/allocation-light
/// is a target direction, while some transitional diagnostics are still present.
struct StepKernelState {
    /// Transitional diagnostics retained for facade compatibility.
    Contracts::CoreMode core_mode{ Contracts::CoreMode::LegacyOnly };
    /// Forces legacy routing even when candidate modes are requested.
    bool force_legacy_only{ true };
    /// Last per-step legacy-vs-candidate comparison result.
    std::optional<Contracts::StepCompareDiagnostic> last_step_compare_diagnostic;
    /// Last account-facade routing diagnostic.
    std::optional<Contracts::AccountFacadeBridgeDiagnostic> last_account_facade_bridge_diagnostic;
    /// Last replay/session coexistence diagnostic.
    std::optional<Contracts::SessionReplayCoexistenceDiagnostic> last_session_replay_coexistence_diagnostic;
    /// Last replay-frame classification emitted by the kernel.
    std::optional<Contracts::ReplayFrameV2Diagnostic> last_replay_frame_v2_diagnostic;
    /// Last candidate trading-session usage diagnostic.
    std::optional<Contracts::TradingSessionCoreV2Diagnostic> last_trading_session_core_v2_diagnostic;
    /// Last top-level exchange facade routing diagnostic.
    std::optional<Contracts::BinanceExchangeFacadeBridgeDiagnostic> last_exchange_facade_bridge_diagnostic;
    /// Last funding/reference-price resolution diagnostic.
    std::optional<Contracts::ReferenceFundingResolverDiagnostic> last_reference_funding_resolver_diagnostic;
    /// Stable run identifier propagated to logs and channels.
    uint64_t run_id{ 0 };

    /// Fixed replay symbol universe and associated market data.
    std::vector<std::string> symbols;
    std::unordered_map<std::string, size_t> symbol_to_id;
    std::vector<QTrading::Dto::Trading::InstrumentType> symbol_instrument_type_by_id;
    std::vector<QTrading::Dto::Trading::InstrumentSpec> symbol_spec_by_id;
    std::shared_ptr<const std::vector<std::string>> symbols_shared;
    std::vector<MarketData> market_data;
    std::vector<FundingRateData> funding_data_pool;
    std::vector<MarketData> mark_data_pool;
    std::vector<MarketData> index_data_pool;
    std::vector<int32_t> funding_data_id_by_symbol;
    std::vector<int32_t> mark_data_id_by_symbol;
    std::vector<int32_t> index_data_id_by_symbol;
    std::vector<size_t> funding_cursor_by_symbol;
    std::vector<size_t> mark_cursor_by_symbol;
    std::vector<size_t> index_cursor_by_symbol;
    std::vector<uint64_t> next_funding_ts_by_symbol;
    std::vector<uint64_t> next_mark_ts_by_symbol;
    std::vector<uint64_t> next_index_ts_by_symbol;
    std::vector<uint8_t> has_next_funding_ts;
    std::vector<uint8_t> has_next_mark_ts;
    std::vector<uint8_t> has_next_index_ts;
    std::vector<uint64_t> last_applied_funding_time_by_symbol;
    /// Last funding row observed for each symbol, whether applied or not.
    std::vector<std::optional<QTrading::Dto::Market::Binance::FundingRateDto>> last_observed_funding_by_symbol;
    /// Count of funding applications performed by the kernel.
    uint64_t funding_applied_events_total{ 0 };
    /// Count of funding rows skipped because no usable mark price existed.
    uint64_t funding_skipped_no_mark_total{ 0 };
    std::vector<size_t> replay_cursor;
    std::vector<uint64_t> next_ts_by_symbol;
    std::vector<uint8_t> has_next_ts;
    std::priority_queue<StepKernelHeapItem, std::vector<StepKernelHeapItem>, StepKernelHeapItemGreater> next_ts_heap;
    std::priority_queue<StepKernelHeapItem, std::vector<StepKernelHeapItem>, StepKernelHeapItemGreater> next_funding_ts_heap;
    /// Monotonic successful-step sequence used by logs and replay diagnostics.
    uint64_t step_seq{ 0 };
    /// True once all public channels have been closed.
    bool channels_closed{ false };
    /// Cached account state version after the current step mutations.
    uint64_t account_state_version{ 0 };
    /// Last status version already logged, used by reduced log debouncing.
    uint64_t last_logged_status_version{ std::numeric_limits<uint64_t>::max() };
    /// Resolved logger module ids cached for fast hot-path emission.
    uint32_t log_module_account_id{ 0 };
    uint32_t log_module_position_id{ 0 };
    uint32_t log_module_order_id{ 0 };
    uint32_t log_module_market_event_id{ 0 };
    uint32_t log_module_funding_event_id{ 0 };
    uint32_t log_module_account_event_id{ 0 };
    uint32_t log_module_position_event_id{ 0 };
    uint32_t log_module_order_event_id{ 0 };
    /// True once logger module ids have been resolved from the sink.
    bool has_resolved_log_module_ids{ false };
    /// Last account version already published to the outward channels.
    uint64_t last_published_account_state_version{ 0 };
    /// Last published position snapshot used by channel gating.
    std::vector<QTrading::dto::Position> last_published_positions;
    /// Last published order snapshot used by channel gating.
    std::vector<QTrading::dto::Order> last_published_orders;
    bool has_published_positions{ false };
    bool has_published_orders{ false };
    /// Last event-emitted position snapshot used for reduced event diffs.
    std::vector<QTrading::dto::Position> last_event_positions;
    /// Last event-emitted order snapshot used for reduced event diffs.
    std::vector<QTrading::dto::Order> last_event_orders;
    /// Position snapshot captured at step entry for same-step comparisons.
    std::vector<QTrading::dto::Position> step_entry_positions;
    /// Order snapshot captured at step entry for same-step comparisons.
    std::vector<QTrading::dto::Order> step_entry_orders;
    /// Position snapshot captured immediately after funding application.
    std::vector<QTrading::dto::Position> funding_apply_positions;
    bool has_funding_apply_positions{ false };
    /// True when current-step event snapshots have been materialized.
    bool has_event_snapshots{ false };
    /// Last wallet balance emitted in reduced account-event logging.
    double last_event_wallet_balance{ 0.0 };
    bool has_last_event_wallet_balance{ false };
    /// Scratch fills produced by MatchingEngine for the current step.
    std::vector<Domain::MatchFill> match_fills_scratch;
    /// Scratch order vector reused when rebuilding the open-order book.
    std::vector<QTrading::dto::Order> matching_orders_next_scratch;
    /// Scratch indices into the active open-order book.
    std::vector<size_t> matching_order_index_scratch;
    /// Scratch liquidity pools for generic matching logic.
    std::vector<double> matching_liquidity_scratch;
    /// Scratch buy-side liquidity pools split for heuristic matching.
    std::vector<double> matching_buy_liquidity_scratch;
    /// Scratch sell-side liquidity pools split for heuristic matching.
    std::vector<double> matching_sell_liquidity_scratch;
    /// Scratch availability flags paired with liquidity vectors.
    std::vector<uint8_t> matching_has_liquidity_scratch;
    /// Scratch reducible-long quantities used by reduce-only checks.
    std::vector<double> matching_reducible_long_scratch;
    /// Scratch reducible-short quantities used by reduce-only checks.
    std::vector<double> matching_reducible_short_scratch;
    /// Scratch mark prices used by liquidation evaluation.
    std::vector<double> liquidation_mark_price_scratch;
    /// Scratch mark-availability flags used by liquidation evaluation.
    std::vector<uint8_t> liquidation_has_mark_scratch;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
