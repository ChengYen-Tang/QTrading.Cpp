#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderCommandRequest.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeDiagnostics.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"
#include "Logger.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Non-step-core runtime state retained by the facade.
/// Holds account/order/channel-adjacent data that is not part of replay cursors.
struct BinanceExchangeRuntimeState {
    std::shared_ptr<QTrading::Log::Logger> logger{};
    bool hedge_mode{ false };
    bool strict_binance_mode{ true };
    bool merge_positions_enabled{ true };
    std::vector<QTrading::dto::Position> positions;
    std::vector<QTrading::dto::Order> orders;
    std::vector<Contracts::AsyncOrderAck> async_order_acks;
    std::unordered_map<std::string, double> symbol_leverage;
    double spot_open_order_initial_margin{ 0.0 };
    double perp_open_order_initial_margin{ 0.0 };
    Contracts::StatusSnapshot last_status_snapshot{};
    Contracts::EventPublishMode event_publish_mode{ Contracts::EventPublishMode::LegacyDirect };
    Contracts::SideEffectAdapterConfig side_effect_adapters{};
    Config::SimulationConfig simulation_config{};
    std::optional<Contracts::EventPublishCompareDiagnostic> last_event_publish_compare_diagnostic;
    size_t order_latency_bars{ 0 };
    uint64_t next_order_id{ 1 };
    uint64_t next_async_order_request_id{ 1 };
    std::deque<Contracts::DeferredOrderCommand> deferred_order_commands;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
