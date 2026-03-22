#pragma once

#include <cstdint>
#include <memory>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Builds one replay frame from StepKernelState by merging symbol timelines.
/// This kernel is performance-sensitive: it mutates cursors in-place and returns
/// exactly one shared DTO payload per step.
class MarketReplayKernel final {
public:
    /// Output of one replay extraction attempt.
    struct StepFrame {
        /// True when a valid step payload was produced.
        bool has_next{ false };
        /// Exchange timestamp chosen for this step.
        uint64_t ts_exchange{ 0 };
        /// Shared market payload pushed to channels/output projections.
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> market_payload;
    };

    /// Produces the next market replay frame and advances internal cursors.
    /// Returns {has_next=false} when replay input is exhausted.
    static StepFrame Next(State::StepKernelState& state);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
