#pragma once

#include <cstdint>

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Minimal scheduler ticket identifying when an async order should resolve.
struct AsyncOrderScheduleTicket {
    /// Async request identifier.
    uint64_t request_id{ 0 };
    /// Step when the request was submitted.
    uint64_t submitted_step{ 0 };
    /// Step when the request should be dequeued and resolved.
    uint64_t due_step{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
