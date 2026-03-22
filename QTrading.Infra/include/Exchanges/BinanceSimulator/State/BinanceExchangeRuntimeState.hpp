#pragma once

#include <optional>
#include <vector>

#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeDiagnostics.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Non-step-core runtime state retained by the facade.
/// Holds account/order/channel-adjacent data that is not part of replay cursors.
struct BinanceExchangeRuntimeState {
    std::vector<QTrading::dto::Position> positions;
    std::vector<QTrading::dto::Order> orders;
    std::vector<Contracts::AsyncOrderAck> async_order_acks;
    Contracts::StatusSnapshot last_status_snapshot{};
    Contracts::EventPublishMode event_publish_mode{ Contracts::EventPublishMode::LegacyDirect };
    Contracts::SideEffectAdapterConfig side_effect_adapters{};
    Config::SimulationConfig simulation_config{};
    std::optional<Contracts::EventPublishCompareDiagnostic> last_event_publish_compare_diagnostic;
    size_t order_latency_bars{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
