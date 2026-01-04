#pragma once

#include <vector>

#include "Logging/StepLogContext.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"

namespace QTrading::Infra::Logging {

    struct MarketEventBuffer {
        StepLogContext* ctx{};
        std::vector<QTrading::Log::FileLogger::FeatherV2::MarketEventDto> events;

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
            }
            events.emplace_back(std::move(e));
        }
    };

} // namespace QTrading::Infra::Logging
