#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "IExecutionEngine.hpp"
#include "Exchanges/IExchange.h"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Execution {

/// @brief Execution engine that converts target notionals into market orders.
class MarketExecutionEngine final : public IExecutionEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        double min_notional = 5.0;
        /// @brief Minimum milliseconds between non-reducing carry rebalances per symbol.
        uint64_t carry_rebalance_cooldown_ms = 180ull * 60ull * 1000ull;
        /// @brief Max fraction of target notional to adjust in one carry rebalance.
        double carry_max_rebalance_step_ratio = 0.012;
        /// @brief For large target notionals, step ratio is clamped by this value.
        double carry_large_notional_step_ratio = 0.006;
        /// @brief Threshold for activating large-notional step clamp.
        double carry_large_notional_threshold = 150000.0;
        /// @brief Minimum milliseconds between carry rebalances for large notionals.
        uint64_t carry_large_notional_cooldown_ms = 360ull * 60ull * 1000ull;
        /// @brief Max fraction of current-bar quote volume used for one carry rebalance.
        double carry_max_participation_rate = 0.0004;
        /// @brief Bootstrap mode activates when current gap/target ratio exceeds this threshold.
        double carry_bootstrap_gap_ratio = 0.25;
        /// @brief In bootstrap mode, allow larger one-step adjustment.
        double carry_bootstrap_step_ratio = 0.50;
        /// @brief In bootstrap mode, allow a higher participation cap.
        double carry_bootstrap_participation_rate = 0.01;
        /// @brief In bootstrap mode, use shorter cooldown for faster initial convergence.
        uint64_t carry_bootstrap_cooldown_ms = 5ull * 60ull * 1000ull;
        /// @brief Scales carry min rebalance notional with target notionals (e.g. 0.000125 * 200000 = 25).
        double carry_min_rebalance_notional_ratio = 0.00025;
        /// @brief Max number of carry rebalances per symbol per UTC day (0 = disabled).
        uint32_t carry_max_rebalances_per_day = 6;
    };

    MarketExecutionEngine(
        std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
            std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
        Config cfg);

    std::vector<ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange_;
    Config cfg_;
    double carry_min_rebalance_notional_{ 20.0 };
    std::unordered_map<std::string, uint64_t> last_carry_order_ts_by_symbol_;
    std::unordered_map<std::string, uint64_t> carry_day_key_by_symbol_;
    std::unordered_map<std::string, uint32_t> carry_rebalance_count_by_symbol_;
    std::unordered_map<std::string, std::size_t> symbol_to_id_;
    bool has_symbol_index_{ false };
};

} // namespace QTrading::Execution
