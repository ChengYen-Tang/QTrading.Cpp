#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "Exchanges/BinanceSimulator/Domain/AsyncOrderScheduleTicket.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class AsyncOrderLatencyScheduler final {
public:
    using PendingAckEmitter = std::function<void(const AsyncOrderScheduleTicket&)>;
    using DeferredEnqueuer = std::function<void(const AsyncOrderScheduleTicket&)>;

    static std::optional<AsyncOrderScheduleTicket> TrySchedule(
        size_t order_latency_bars,
        uint64_t processed_steps,
        uint64_t& next_async_order_request_id,
        const PendingAckEmitter& emit_pending_ack,
        const DeferredEnqueuer& enqueue_deferred);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
