#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Coordinates one exchange step using the rebuilt application pipeline.
/// This is a hot-path orchestrator and intentionally delegates heavy work to
/// specialized kernels/builders to keep the facade thin.
class StepKernel final {
public:
    explicit StepKernel(BinanceExchange& exchange) noexcept;

    /// Advances replay by one step and emits observable outputs.
    /// Returns false only when replay termination is reached.
    bool run_step() const;

private:
    /// Non-owning facade reference; lifetime is managed by BinanceExchange.
    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
