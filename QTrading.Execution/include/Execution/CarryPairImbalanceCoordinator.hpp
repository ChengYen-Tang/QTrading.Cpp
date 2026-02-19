#pragma once

#include "Execution/IPairCoordinator.hpp"

#include <unordered_map>

namespace QTrading::Execution {

/// @brief Final carry-order guardrail to reduce one-sided imbalance and oversized slices.
class CarryPairImbalanceCoordinator final : public IPairCoordinator {
public:
    struct Config {
        /// @brief Master switch for this coordinator.
        bool enabled = false;
        /// @brief Apply this coordinator only for funding_carry strategy.
        bool apply_only_funding_carry = true;
        /// @brief Apply this coordinator only under low urgency.
        bool apply_only_low_urgency = true;
        /// @brief Ignore reduce-only orders when computing carry side balance.
        bool ignore_reduce_only_orders = true;
        /// @brief Require both buy and sell sides in same tick; otherwise drop carry orders.
        bool require_two_sided = false;
        /// @brief Clip larger side notional to smaller side notional.
        bool balance_two_sided = true;
        /// @brief Min notional for kept carry orders.
        double min_notional_usdt = 5.0;
        /// @brief Max per-symbol order notional cap. <=0 means disabled.
        double max_leg_notional_usdt = 0.0;
        /// @brief Cap each carry order by current-bar quote volume participation.
        bool cap_by_quote_volume = false;
        /// @brief Participation rate for quote-volume cap.
        double max_participation_rate = 0.0;
        /// @brief Enable confidence-adaptive scaling for notional caps.
        bool carry_confidence_adaptive_enabled = true;
        /// @brief Confidence scale range for max leg notional.
        double carry_confidence_max_leg_scale_min = 1.0;
        double carry_confidence_max_leg_scale_max = 1.0;
        /// @brief Confidence scale range for quote-volume participation cap.
        double carry_confidence_participation_scale_min = 1.0;
        double carry_confidence_participation_scale_max = 1.0;
    };

    explicit CarryPairImbalanceCoordinator(Config cfg = {});

    std::vector<ExecutionOrder> Coordinate(
        std::vector<ExecutionOrder> orders,
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    std::unordered_map<std::string, std::size_t> symbol_to_id_;
    bool has_symbol_index_{ false };
};

} // namespace QTrading::Execution
