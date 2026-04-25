#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Centralizes end-of-replay decisions and one-time channel close behavior.
class TerminationPolicy final {
public:
    /// Checks if no future replay timestamp is available.
    static bool IsReplayExhausted(const State::StepKernelState& state) noexcept;
    /// Closes public channels once and marks state as terminated.
    static void CloseChannels(BinanceExchange& exchange, State::StepKernelState& state) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
