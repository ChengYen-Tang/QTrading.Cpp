#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "Exchanges/BinanceSimulator/Domain/AsyncOrderScheduleTicket.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Stateless helper that converts a sync command into pending/deferred async scheduling.
class AsyncOrderLatencyScheduler final {
public:
    /// Emits a pending ack immediately when scheduling occurs.
    using PendingAckEmitter = std::function<void(const AsyncOrderScheduleTicket&)>;
    /// Enqueues the deferred command ticket for later step resolution.
    using DeferredEnqueuer = std::function<void(const AsyncOrderScheduleTicket&)>;

    /// Returns a schedule ticket when latency simulation defers the command.
    static std::optional<AsyncOrderScheduleTicket> TrySchedule(
        size_t order_latency_bars,
        uint64_t processed_steps,
        uint64_t& next_async_order_request_id,
        const PendingAckEmitter& emit_pending_ack,
        const DeferredEnqueuer& enqueue_deferred);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
