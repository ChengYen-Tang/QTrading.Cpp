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
    /// Shared logger sink used by reduced observability and event emission.
    std::shared_ptr<QTrading::Log::Logger> logger{};
    /// Current account position mode mirrored from bootstrap/config APIs.
    bool hedge_mode{ false };
    /// Enables stricter Binance-style validation paths.
    bool strict_binance_mode{ true };
    /// Allows fill settlement to merge compatible positions when true.
    bool merge_positions_enabled{ true };
    /// Active VIP level used by fee-rate lookup.
    int vip_level{ 0 };
    /// Live position book owned by the facade runtime.
    std::vector<QTrading::dto::Position> positions;
    /// Live open-order book owned by the facade runtime.
    std::vector<QTrading::dto::Order> orders;
    /// Deferred async-ack records waiting to resolve or already resolved.
    std::vector<Contracts::AsyncOrderAck> async_order_acks;
    /// Per-symbol leverage overrides exposed through the public facade.
    std::unordered_map<std::string, double> symbol_leverage;
    /// Spot open-order margin reservation cached from the current order book.
    double spot_open_order_initial_margin{ 0.0 };
    /// Perp open-order margin reservation cached from the current order book.
    double perp_open_order_initial_margin{ 0.0 };
    /// Last published/readable status snapshot.
    Contracts::StatusSnapshot last_status_snapshot{};
    /// Event side-effect publication mode for the current runtime.
    Contracts::EventPublishMode event_publish_mode{ Contracts::EventPublishMode::LegacyDirect };
    /// Optional side-effect forwarders used by tests and transitional integrations.
    Contracts::SideEffectAdapterConfig side_effect_adapters{};
    /// Mutable simulation knobs consumed by replay, matching, and liquidation logic.
    Config::SimulationConfig simulation_config{};
    /// Last legacy-vs-candidate event publish comparison result.
    std::optional<Contracts::EventPublishCompareDiagnostic> last_event_publish_compare_diagnostic;
    /// Counter of orders blocked by basis-stress protection.
    uint64_t basis_stress_blocked_orders_total{ 0 };
    /// Fixed order-latency delay measured in replay bars.
    size_t order_latency_bars{ 0 };
    /// Next synchronous order id assigned by the runtime.
    uint64_t next_order_id{ 1 };
    /// Next async request id assigned by the runtime.
    uint64_t next_async_order_request_id{ 1 };
    /// Pending deferred commands scheduled by the async latency path.
    std::deque<Contracts::DeferredOrderCommand> deferred_order_commands;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
