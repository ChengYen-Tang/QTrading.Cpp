#pragma once

#include "Exchanges/BinanceSimulator/Application/MarketReplayStepFrame.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Builds one replay frame from StepKernelState by merging symbol timelines.
/// This kernel is performance-sensitive: it mutates cursors in-place and returns
/// exactly one shared DTO payload per step.
class MarketReplayKernel final {
public:
    /// Produces the next market replay frame and advances internal cursors.
    /// Returns {has_next=false} when replay input is exhausted.
    static MarketReplayStepFrame Next(State::StepKernelState& state);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
