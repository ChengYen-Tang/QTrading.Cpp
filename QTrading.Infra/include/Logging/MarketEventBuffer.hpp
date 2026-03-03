#pragma once

#include <memory_resource>

#include "Logging/StepLogContext.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"

namespace QTrading::Infra::Logging {

    struct MarketEventBuffer {
        StepLogContext* ctx{};
        std::pmr::vector<QTrading::Log::FileLogger::FeatherV2::MarketEventDto> events;

        explicit MarketEventBuffer(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
            : events(mr) {}

        void reset(StepLogContext& c)
        {
            ctx = &c;
            events.clear();
        }

        void reserve(size_t n) { events.reserve(n); }

        void push(QTrading::Log::FileLogger::FeatherV2::MarketEventDto&& e)
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
