#pragma once

#include <cstdint>
#include <optional>

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
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
