#pragma once

#include <cstdint>

namespace QTrading::Dto::Market::Binance {
struct MultiKlineDto;
}

namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class LiquidationExecution final {
public:
    static bool Run(
        State::BinanceExchangeRuntimeState& runtime_state,
        Account& account,
        State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
