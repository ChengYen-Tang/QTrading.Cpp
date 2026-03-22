#pragma once

#include <cstdint>

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct AsyncOrderScheduleTicket {
    uint64_t request_id{ 0 };
    uint64_t submitted_step{ 0 };
    uint64_t due_step{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
