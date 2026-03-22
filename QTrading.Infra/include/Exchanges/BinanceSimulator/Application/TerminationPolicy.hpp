#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

class TerminationPolicy final {
public:
    static bool IsReplayExhausted(const State::StepKernelState& state) noexcept;
    static void CloseChannels(BinanceExchange& exchange, State::StepKernelState& state) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
