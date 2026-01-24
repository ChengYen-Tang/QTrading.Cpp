#pragma once

#include <vector>

#include "Logging/StepLogContext.hpp"
#include "FileLogger/FeatherV2/FundingEvent.hpp"

namespace QTrading::Infra::Logging {

    struct FundingEventBuffer {
        StepLogContext* ctx{};
        std::vector<QTrading::Log::FileLogger::FeatherV2::FundingEventDto> events;

        void reset(StepLogContext& c)
        {
            ctx = &c;
            events.clear();
        }

        void reserve(size_t n) { events.reserve(n); }

        void push(QTrading::Log::FileLogger::FeatherV2::FundingEventDto&& e)
        {
            if (ctx) {
                e.run_id = ctx->run_id;
                e.step_seq = ctx->step_seq;
                e.event_seq = ctx->next_event_seq();
                e.ts_local = ctx->ts_local;
            }
            events.emplace_back(std::move(e));
        }
    };

} // namespace QTrading::Infra::Logging
