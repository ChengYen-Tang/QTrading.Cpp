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
    bool force_legacy_only{ true };
    std::optional<Contracts::StepCompareDiagnostic> last_step_compare_diagnostic;
    std::optional<Contracts::AccountFacadeBridgeDiagnostic> last_account_facade_bridge_diagnostic;
    std::optional<Contracts::SessionReplayCoexistenceDiagnostic> last_session_replay_coexistence_diagnostic;
    std::optional<Contracts::ReplayFrameV2Diagnostic> last_replay_frame_v2_diagnostic;
    std::optional<Contracts::TradingSessionCoreV2Diagnostic> last_trading_session_core_v2_diagnostic;
    std::optional<Contracts::BinanceExchangeFacadeBridgeDiagnostic> last_exchange_facade_bridge_diagnostic;
    std::optional<Contracts::ReferenceFundingResolverDiagnostic> last_reference_funding_resolver_diagnostic;
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
    std::vector<std::optional<QTrading::Dto::Market::Binance::FundingRateDto>> last_observed_funding_by_symbol;
    uint64_t funding_applied_events_total{ 0 };
    uint64_t funding_skipped_no_mark_total{ 0 };
    std::vector<size_t> replay_cursor;
    std::vector<uint64_t> next_ts_by_symbol;
    std::vector<uint8_t> has_next_ts;
    std::priority_queue<StepKernelHeapItem, std::vector<StepKernelHeapItem>, StepKernelHeapItemGreater> next_ts_heap;
    std::priority_queue<StepKernelHeapItem, std::vector<StepKernelHeapItem>, StepKernelHeapItemGreater> next_funding_ts_heap;
    uint64_t step_seq{ 0 };
    bool channels_closed{ false };
    uint64_t account_state_version{ 0 };
    uint64_t last_logged_status_version{ std::numeric_limits<uint64_t>::max() };
    uint32_t log_module_account_id{ 0 };
    uint32_t log_module_market_event_id{ 0 };
    uint32_t log_module_funding_event_id{ 0 };
    bool has_resolved_log_module_ids{ false };
    uint64_t last_published_account_state_version{ 0 };
    std::vector<QTrading::dto::Position> last_published_positions;
    std::vector<QTrading::dto::Order> last_published_orders;
    bool has_published_positions{ false };
    bool has_published_orders{ false };
    std::vector<Domain::MatchFill> match_fills_scratch;
    std::vector<QTrading::dto::Order> matching_orders_next_scratch;
    std::vector<size_t> matching_order_index_scratch;
    std::vector<double> matching_liquidity_scratch;
    std::vector<double> matching_buy_liquidity_scratch;
    std::vector<double> matching_sell_liquidity_scratch;
    std::vector<uint8_t> matching_has_liquidity_scratch;
    std::vector<double> matching_reducible_long_scratch;
    std::vector<double> matching_reducible_short_scratch;
    std::vector<double> liquidation_mark_price_scratch;
    std::vector<uint8_t> liquidation_has_mark_scratch;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
