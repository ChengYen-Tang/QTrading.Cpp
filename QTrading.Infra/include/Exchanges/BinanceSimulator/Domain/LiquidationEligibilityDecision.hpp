#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace QTrading::Dto::Market::Binance {
struct MultiKlineDto;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct LiquidationHealthSnapshot {
    bool has_perp_positions{ false };
    bool has_full_mark_context{ false };
    bool distressed{ false };
    double equity{ 0.0 };
    double maintenance_margin{ 0.0 };
};

class LiquidationEligibilityDecision final {
public:
    static LiquidationHealthSnapshot Evaluate(
        State::BinanceExchangeRuntimeState& runtime_state,
        const Account& account,
        const State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload,
        std::vector<double>& mark_price_scratch,
        std::vector<uint8_t>& has_mark_scratch) noexcept;

    static int FindWorstLossPerpPositionIndex(
        const State::BinanceExchangeRuntimeState& runtime_state,
        const State::StepKernelState& step_state,
        const std::vector<uint8_t>& has_mark_scratch) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
