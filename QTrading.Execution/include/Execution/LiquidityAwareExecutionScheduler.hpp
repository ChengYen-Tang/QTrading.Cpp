#pragma once

#include "Execution/IExecutionScheduler.hpp"

#include <cstdint>
#include <unordered_map>

namespace QTrading::Execution {

/// @brief Scheduler that can cap one-tick carry delta by observed bar quote volume.
/// @details
///   When enabled, this scheduler computes a per-symbol current notional snapshot from
///   account positions (+ optional open orders), then limits the parent delta:
///   `delta = target - current` to `[-quote_volume * rate, +quote_volume * rate]`.
///   The output slice target becomes `current + clipped_delta`.
class LiquidityAwareExecutionScheduler final : public IExecutionScheduler {
public:
    struct Config {
        /// @brief Enable quote-volume based delta capping.
        bool carry_delta_participation_cap_enabled = false;
        /// @brief Max fraction of current-bar quote volume used for one scheduler slice.
        double carry_delta_participation_rate = 0.0;
        /// @brief Drop scheduler slices whose clipped delta notional is smaller than this.
        double carry_min_slice_notional_usdt = 0.0;
        /// @brief Apply this scheduler cap only for funding_carry low-urgency flow.
        bool carry_apply_only_low_urgency = true;
        /// @brief Include open orders when estimating current notional.
        bool include_open_orders_in_current_notional = true;
        /// @brief Enable confidence-adaptive scaling for participation rate.
        bool carry_confidence_adaptive_enabled = true;
        /// @brief Participation rate scale range mapped by signal confidence.
        double carry_confidence_rate_scale_min = 1.0;
        double carry_confidence_rate_scale_max = 1.0;
        /// @brief Enable gap-adaptive scaling based on |delta| / |target|.
        bool carry_gap_adaptive_enabled = false;
        /// @brief Gap ratio reference where scaling reaches max.
        double carry_gap_reference_ratio = 0.25;
        /// @brief Participation rate scale range mapped by gap ratio.
        double carry_gap_rate_scale_min = 1.0;
        double carry_gap_rate_scale_max = 1.0;
        /// @brief Enable per-window notional budget to smooth large-cap execution.
        bool carry_window_budget_enabled = false;
        /// @brief Window size in milliseconds used by budget accounting.
        uint64_t carry_window_budget_ms = 8ull * 60ull * 60ull * 1000ull;
        /// @brief Max fraction of cumulative quote volume consumable per budget window.
        double carry_window_quote_participation_rate = 0.0;
        /// @brief Hard per-window notional cap. <= 0 means disabled.
        double carry_window_max_notional_usdt = 0.0;
        /// @brief Enable confidence-adaptive scaling for window budget caps.
        bool carry_window_confidence_adaptive_enabled = true;
        /// @brief Confidence scale range applied to window budget caps.
        double carry_window_confidence_scale_min = 1.0;
        double carry_window_confidence_scale_max = 1.0;
        /// @brief Batch only carry notional increases; reductions still pass through immediately.
        bool carry_increase_batching_enabled = false;
        /// @brief Minimum interval between carry target-notional increase updates.
        uint64_t carry_increase_batch_ms = 60ull * 60ull * 1000ull;
        /// @brief Minimum absolute target-notional change required before applying batch update.
        double carry_increase_batch_min_update_notional = 0.0;
        /// @brief Minimum relative target-notional change required before applying batch update.
        double carry_increase_batch_min_update_ratio = 0.0;
    };

    explicit LiquidityAwareExecutionScheduler(Config cfg = {});

    std::vector<ExecutionSlice> BuildSlices(
        const std::vector<ExecutionParentOrder>& parent_orders,
        const QTrading::Risk::AccountState& account,
        const ExecutionSignal& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    std::unordered_map<std::string, std::size_t> symbol_to_id_;
    std::unordered_map<std::string, uint64_t> budget_window_key_by_symbol_;
    std::unordered_map<std::string, double> budget_consumed_notional_by_symbol_;
    std::unordered_map<std::string, double> budget_cumulative_quote_volume_by_symbol_;
    std::unordered_map<std::string, uint64_t> last_increase_batch_ts_by_symbol_;
    std::unordered_map<std::string, double> batched_target_notional_by_symbol_;
    bool has_symbol_index_{ false };
};

} // namespace QTrading::Execution
