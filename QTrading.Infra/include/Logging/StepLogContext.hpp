#pragma once

#include <cstdint>

namespace QTrading::Infra::Logging {

    struct StepLogContext {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t ts_exchange{};
        uint64_t event_seq{};

        uint64_t next_event_seq() noexcept { return event_seq++; }
    };

} // namespace QTrading::Infra::Logging
