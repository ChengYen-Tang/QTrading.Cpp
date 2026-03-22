#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "Data/Binance/MarketData.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeDiagnostics.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

struct StepKernelState {
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

    struct HeapItem {
        uint64_t ts{ 0 };
        size_t sym_id{ 0 };
    };
    struct HeapItemGreater {
        bool operator()(const HeapItem& a, const HeapItem& b) const noexcept
        {
            if (a.ts != b.ts) {
                return a.ts > b.ts;
            }
            return a.sym_id > b.sym_id;
        }
    };

    std::vector<std::string> symbols;
    std::shared_ptr<const std::vector<std::string>> symbols_shared;
    std::vector<MarketData> market_data;
    std::vector<size_t> replay_cursor;
    std::vector<uint64_t> next_ts_by_symbol;
    std::vector<uint8_t> has_next_ts;
    std::priority_queue<HeapItem, std::vector<HeapItem>, HeapItemGreater> next_ts_heap;
    uint64_t step_seq{ 0 };
    bool channels_closed{ false };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
