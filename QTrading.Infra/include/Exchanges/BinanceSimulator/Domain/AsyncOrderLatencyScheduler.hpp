#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class AsyncOrderLatencyScheduler final {
public:
    struct ScheduleTicket {
        uint64_t request_id{ 0 };
        uint64_t submitted_step{ 0 };
        uint64_t due_step{ 0 };
    };

    using PendingAckEmitter = std::function<void(const ScheduleTicket&)>;
    using DeferredEnqueuer = std::function<void(const ScheduleTicket&)>;

    static std::optional<ScheduleTicket> TrySchedule(
        size_t order_latency_bars,
        uint64_t processed_steps,
        uint64_t& next_async_order_request_id,
        const PendingAckEmitter& emit_pending_ack,
        const DeferredEnqueuer& enqueue_deferred)
    {
        if (order_latency_bars == 0) {
            return std::nullopt;
        }

        ScheduleTicket ticket{};
        ticket.request_id = next_async_order_request_id++;
        ticket.submitted_step = processed_steps;
        ticket.due_step = processed_steps + static_cast<uint64_t>(order_latency_bars);

        if (emit_pending_ack) {
            emit_pending_ack(ticket);
        }
        if (enqueue_deferred) {
            enqueue_deferred(ticket);
        }

        return ticket;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
