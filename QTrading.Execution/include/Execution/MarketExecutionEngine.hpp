#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "IExecutionEngine.hpp"
#include "Exchanges/IExchange.h"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Execution {

/// @brief Execution engine that converts target notionals into executable orders.
class MarketExecutionEngine final : public IExecutionEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        double min_notional = 5.0;
        /// @brief Minimum milliseconds between non-reducing carry rebalances per symbol.
        uint64_t carry_rebalance_cooldown_ms = 480ull * 60ull * 1000ull;
        /// @brief Max fraction of target notional to adjust in one carry rebalance.
        double carry_max_rebalance_step_ratio = 0.010;
        /// @brief For large target notionals, step ratio is clamped by this value.
        double carry_large_notional_step_ratio = 0.006;
        /// @brief Threshold for activating large-notional step clamp.
        double carry_large_notional_threshold = 150000.0;
        /// @brief Minimum milliseconds between carry rebalances for large notionals.
        uint64_t carry_large_notional_cooldown_ms = 360ull * 60ull * 1000ull;
        /// @brief Max fraction of current-bar quote volume used for one carry rebalance.
        double carry_max_participation_rate = 0.0003;
        /// @brief Optional cumulative execution budget on top of per-bar participation.
        bool carry_window_budget_enabled = false;
        /// @brief Window size for cumulative quote-volume budget.
        uint64_t carry_window_budget_ms = 6ull * 60ull * 60ull * 1000ull;
        /// @brief Max fraction of cumulative quote volume executable inside one window.
        double carry_window_budget_participation_rate = 0.0020;
        /// @brief Bootstrap mode activates when current gap/target ratio exceeds this threshold.
        double carry_bootstrap_gap_ratio = 0.25;
        /// @brief In bootstrap mode, allow larger one-step adjustment.
        double carry_bootstrap_step_ratio = 0.50;
        /// @brief In bootstrap mode, allow a higher participation cap.
        double carry_bootstrap_participation_rate = 0.01;
        /// @brief In bootstrap mode, use shorter cooldown for faster initial convergence.
        uint64_t carry_bootstrap_cooldown_ms = 5ull * 60ull * 1000ull;
        /// @brief Scales carry min rebalance notional with target notionals (e.g. 0.000125 * 200000 = 25).
        double carry_min_rebalance_notional_ratio = 0.00030;
        /// @brief Max number of carry rebalances per symbol per UTC day (0 = disabled).
        uint32_t carry_max_rebalances_per_day = 8;
        /// @brief Enable confidence-to-execution mapping for carry rebalances.
        bool carry_confidence_adaptive_enabled = true;
        /// @brief Multiplier range for rebalance step ratio: low confidence -> min, high confidence -> max.
        double carry_confidence_step_scale_min = 1.0;
        double carry_confidence_step_scale_max = 1.0;
        /// @brief Multiplier range for participation cap: low confidence -> min, high confidence -> max.
        double carry_confidence_participation_scale_min = 1.0;
        double carry_confidence_participation_scale_max = 1.0;
        /// @brief Cooldown scale range: high confidence -> min, low confidence -> max.
        double carry_confidence_cooldown_scale_min = 1.0;
        double carry_confidence_cooldown_scale_max = 1.0;
        /// @brief If enabled, skip non-reducing carry rebalances unless both buy/sell legs exist in the same tick.
        bool carry_require_two_sided_rebalance = false;
        /// @brief Rebalance non-reducing carry legs by clipping the larger side to match the smaller side notional.
        bool carry_balance_two_sided_rebalance = true;
        /// @brief Prefer passive limit orders for low-urgency carry rebalances.
        ///        This reduces fee/slippage in normal conditions, while catch-up still uses market.
        bool carry_maker_first_enabled = false;
        /// @brief Offset from reference price for maker-first limit orders, in basis points.
        ///        Buy price = ref * (1 - bps*1e-4), sell price = ref * (1 + bps*1e-4).
        double carry_maker_limit_offset_bps = 1.5;
        /// @brief If |delta| / |target| exceeds this threshold, use market instead of maker-first.
        double carry_maker_catchup_gap_ratio = 0.20;
        /// @brief Freeze carry target notional per symbol until target drift exceeds threshold.
        ///        This reduces micro-churn when risk target jitters bar-to-bar.
        bool carry_target_anchor_enabled = false;
        /// @brief Anchor update threshold as ratio of |target_notional|.
        ///        Effective threshold = max(min_rebalance_notional, |target| * ratio).
        double carry_target_anchor_update_ratio = 0.005;
    };

    MarketExecutionEngine(
        std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
            std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
        Config cfg);

    std::vector<ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const ExecutionSignal& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange_;
    Config cfg_;
    double carry_min_rebalance_notional_{ 20.0 };
    std::unordered_map<std::string, uint64_t> last_carry_order_ts_by_symbol_;
    std::unordered_map<std::string, uint64_t> carry_day_key_by_symbol_;
    std::unordered_map<std::string, uint32_t> carry_rebalance_count_by_symbol_;
    std::unordered_map<std::string, double> carry_target_anchor_notional_by_symbol_;
    std::unordered_map<std::string, uint64_t> carry_window_start_ts_by_symbol_;
    std::unordered_map<std::string, double> carry_window_cum_quote_volume_by_symbol_;
    std::unordered_map<std::string, double> carry_window_used_notional_by_symbol_;
    std::unordered_map<std::string, std::size_t> symbol_to_id_;
    bool has_symbol_index_{ false };
};

} // namespace QTrading::Execution
