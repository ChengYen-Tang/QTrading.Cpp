#pragma once

#include <vector>

#include "Logging/StepLogContext.hpp"
#include "FileLogger/FeatherV2/AccountEvent.hpp"

namespace QTrading::Infra::Logging {

    struct AccountEventBuffer {
        StepLogContext* ctx{};
        std::vector<QTrading::Log::FileLogger::FeatherV2::AccountEventDto> events;

        void reset(StepLogContext& c)
        {
            ctx = &c;
            events.clear();
        }

        void reserve(size_t n) { events.reserve(n); }

        void push(QTrading::Log::FileLogger::FeatherV2::AccountEventDto&& e)
        {
            if (ctx) {
                e.run_id = ctx->run_id;
                e.step_seq = ctx->step_seq;
                e.event_seq = ctx->next_event_seq();
            }
            events.emplace_back(std::move(e));
        }
    };

} // namespace QTrading::Infra::Logging
