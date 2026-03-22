#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

class StepKernel final {
public:
    explicit StepKernel(BinanceExchange& exchange) noexcept;

    bool run_step() const;

private:
    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
