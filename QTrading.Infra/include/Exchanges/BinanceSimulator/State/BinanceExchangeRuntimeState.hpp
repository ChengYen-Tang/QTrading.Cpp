#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeDiagnostics.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

struct BinanceExchangeRuntimeState {
    std::vector<QTrading::dto::Position> positions;
    std::vector<QTrading::dto::Order> orders;
    std::vector<Contracts::AsyncOrderAck> async_order_acks;
    Contracts::StatusSnapshot last_status_snapshot{};
    Contracts::CoreMode core_mode{ Contracts::CoreMode::LegacyOnly };
    bool force_legacy_only{ true };
    Contracts::EventPublishMode event_publish_mode{ Contracts::EventPublishMode::LegacyDirect };
    Contracts::SideEffectAdapterConfig side_effect_adapters{};
    Config::SimulationConfig simulation_config{};
    std::optional<Contracts::StepCompareDiagnostic> last_step_compare_diagnostic;
    std::optional<Contracts::AccountFacadeBridgeDiagnostic> last_account_facade_bridge_diagnostic;
    std::optional<Contracts::SessionReplayCoexistenceDiagnostic> last_session_replay_coexistence_diagnostic;
    std::optional<Contracts::ReplayFrameV2Diagnostic> last_replay_frame_v2_diagnostic;
    std::optional<Contracts::TradingSessionCoreV2Diagnostic> last_trading_session_core_v2_diagnostic;
    std::optional<Contracts::BinanceExchangeFacadeBridgeDiagnostic> last_exchange_facade_bridge_diagnostic;
    std::optional<Contracts::EventPublishCompareDiagnostic> last_event_publish_compare_diagnostic;
    std::optional<Contracts::ReferenceFundingResolverDiagnostic> last_reference_funding_resolver_diagnostic;
    size_t order_latency_bars{ 0 };
    uint64_t run_id{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
