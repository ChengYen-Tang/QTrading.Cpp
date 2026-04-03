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

/// Reduced-scope liquidation health result for the current replay step.
struct LiquidationHealthSnapshot {
    /// True when at least one perp position exists.
    bool has_perp_positions{ false };
    /// True when sufficient mark-price context exists to evaluate distress.
    bool has_full_mark_context{ false };
    /// True when the account is considered distressed under the reduced rule set.
    bool distressed{ false };
    /// Current account equity used by the decision.
    double equity{ 0.0 };
    /// Current maintenance margin requirement used by the decision.
    double maintenance_margin{ 0.0 };
    /// Index of the current worst-loss perp position eligible for liquidation.
    int worst_loss_perp_position_index{ -1 };
};

/// Evaluates whether the reduced liquidation path should run for the current step.
class LiquidationEligibilityDecision final {
public:
    /// Computes liquidation health using current account state and replay prices.
    static LiquidationHealthSnapshot Evaluate(
        const State::BinanceExchangeRuntimeState& runtime_state,
        const Account& account,
        const State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload,
        std::vector<double>& mark_price_scratch,
        std::vector<uint8_t>& has_mark_scratch) noexcept;

    /// Returns the index of the worst-loss perp position eligible for liquidation.
    static int FindWorstLossPerpPositionIndex(
        const State::BinanceExchangeRuntimeState& runtime_state,
        const State::StepKernelState& step_state,
        const std::vector<uint8_t>& has_mark_scratch,
        const std::vector<double>& mark_price_scratch) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
