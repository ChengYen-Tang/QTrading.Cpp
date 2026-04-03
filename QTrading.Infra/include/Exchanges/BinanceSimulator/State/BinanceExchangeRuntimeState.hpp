#pragma once

#include <deque>
#include <cstdint>
#include <memory>
#include <functional>
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

struct SymbolFeeRateOverride {
    double maker_fee_rate{ 0.0 };
    double taker_fee_rate{ 0.0 };
};

struct PositionIndexKey {
    size_t symbol_id{ 0 };
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Spot };
    bool is_long{ true };

    bool operator==(const PositionIndexKey& rhs) const noexcept
    {
        return symbol_id == rhs.symbol_id &&
            instrument_type == rhs.instrument_type &&
            is_long == rhs.is_long;
    }
};

struct PositionIndexKeyHash {
    size_t operator()(const PositionIndexKey& key) const noexcept
    {
        const size_t h1 = std::hash<size_t>{}(key.symbol_id);
        const size_t h2 = std::hash<int>{}(static_cast<int>(key.instrument_type));
        const size_t h3 = std::hash<bool>{}(key.is_long);
        return h1 ^ (h2 << 1U) ^ (h3 << 2U);
    }
};

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
    /// Dense symbol ids aligned with `positions` slots (runtime hot path).
    std::vector<size_t> position_symbol_id_by_slot{};
    /// Live open-order book owned by the facade runtime.
    std::vector<QTrading::dto::Order> orders;
    /// Dense symbol ids aligned with `orders` slots (runtime hot path).
    std::vector<size_t> order_symbol_id_by_slot{};
    /// Internal order-id to symbol-id cache for runtime hot paths.
    std::unordered_map<int, size_t> order_symbol_id_by_order_id{};
    /// Deferred async-ack records waiting to resolve or already resolved.
    std::vector<Contracts::AsyncOrderAck> async_order_acks;
    /// Per-symbol leverage overrides exposed through the public facade.
    std::unordered_map<std::string, double> symbol_leverage;
    /// Optional per-symbol spot maker/taker fee-rate overrides.
    std::unordered_map<std::string, SymbolFeeRateOverride> spot_symbol_fee_overrides;
    /// Optional per-symbol perp maker/taker fee-rate overrides.
    std::unordered_map<std::string, SymbolFeeRateOverride> perp_symbol_fee_overrides;
    /// Spot open-order margin reservation cached from the current order book.
    double spot_open_order_initial_margin{ 0.0 };
    /// Perp open-order margin reservation cached from the current order book.
    double perp_open_order_initial_margin{ 0.0 };
    /// Dense cache of spot buy-order reservation by symbol id.
    std::vector<double> spot_open_order_initial_margin_by_symbol{};
    /// Dense cache of perp open-order reservation by symbol id.
    std::vector<double> perp_open_order_initial_margin_by_symbol{};
    /// Dense cache of latest perp reference price by symbol id.
    std::vector<double> perp_reference_price_by_symbol{};
    /// Dense cache of one-way perp net position quantity by symbol id.
    std::vector<double> perp_net_position_qty_by_symbol{};
    /// True when order reservation caches are initialized for current symbol layout.
    bool order_reservation_cache_ready{ false };
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
    /// Monotonic version bumped whenever open-order book mutates.
    uint64_t orders_version{ 0 };
    /// Next position id assigned by fill settlement; 0 means unseeded.
    uint64_t next_position_id{ 0 };
    /// Monotonic version bumped whenever position book mutates.
    uint64_t positions_version{ 0 };
    /// Internal dense symbol-id cache for fill-settlement position indexing.
    std::unordered_map<std::string, size_t> position_symbol_to_id{};
    /// Internal position-id to slot index map for fill-settlement fast lookup.
    std::unordered_map<int, size_t> position_slot_by_id{};
    /// Internal `(symbol_id, instrument_type, side)` to position-id queue index.
    std::unordered_map<PositionIndexKey, std::deque<int>, PositionIndexKeyHash> position_ids_by_key{};
    /// Internal position-id to symbol-id cache for runtime hot paths.
    std::unordered_map<int, size_t> position_symbol_id_by_position_id{};
    /// True when internal fill-settlement position index mirrors `positions`.
    bool position_index_ready{ false };
    /// Next async request id assigned by the runtime.
    uint64_t next_async_order_request_id{ 1 };
    /// Pending deferred commands scheduled by the async latency path.
    std::deque<Contracts::DeferredOrderCommand> deferred_order_commands;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
