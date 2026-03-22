#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

struct StepObservableContext;

/// Emits channel-facing outputs from a prepared StepObservableContext.
/// Keeps facade and kernels free of channel-specific branching.
class ChannelPublisher final {
public:
    /// Publishes market payload for a successful non-terminal step.
    static void PublishStep(BinanceExchange& exchange, const StepObservableContext& context) noexcept;
    /// Closes public channels when a terminal step is observed.
    static void ClosePublicChannels(BinanceExchange& exchange, const StepObservableContext& context) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
