#pragma once

#include <limits>
#include <memory>
#include <deque>
#include <string>
#include <unordered_map>
#include "IRiskEngine.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"

namespace QTrading::Risk {

/// @brief Basic risk engine using fixed notional and leverage caps.
class SimpleRiskEngine final : public IRiskEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        double notional_usdt = 1000.0;
        double leverage = 2.0;
        double max_leverage = 3.0;
        /// @brief Skip rebalancing when |net| / gross exposure is below this ratio.
        double rebalance_threshold_ratio = 0.03;
        /// @brief Soft cap for basis magnitude; scales notional down when exceeded.
        ///        Set to >= 1.0 to disable (default).
        double basis_soft_cap_pct = 1.0;
        /// @brief Minimum notional scale when soft cap is exceeded (0..1).
        double min_notional_scale = 1.0;
        /// @brief Basis trend (EMA slope) soft cap; set to >= 1.0 to disable.
        double basis_trend_max_abs = 1.0;
        /// @brief Minimum notional scale under adverse basis trend (0..1).
        double basis_trend_min_scale = 1.0;
        /// @brief EMA alpha for basis trend calculation (0..1).
        double basis_trend_ema_alpha = 0.05;
        /// @brief Scale notional when basis is below threshold (e.g., negative basis).
        double neg_basis_threshold = 0.0;
        /// @brief Notional scale when basis < threshold (0..1).
        double neg_basis_scale = 1.0;
        /// @brief Hysteresis band around neg_basis_threshold to avoid rapid mode flipping.
        double neg_basis_hysteresis_pct = 0.0;
        /// @brief Basis overlay cap (absolute basis deviation from EMA). Set <= 0 to disable.
        double basis_overlay_cap = 0.0;
        /// @brief Max overlay scale (+/-) applied to notional (0..1).
        double basis_overlay_strength = 0.0;
        /// @brief Allow basis overlay to scale above 1.0 on favorable deviations.
        ///        When false, overlay only scales down notional.
        bool basis_overlay_allow_upscale = false;
        /// @brief Upper bound for basis overlay multiplier when upscale is enabled.
        double basis_overlay_upscale_cap = 1.25;
        /// @brief Lower bound for basis overlay multiplier.
        double basis_overlay_downscale_floor = 0.50;
        /// @brief Minimum |deviation| activation ratio against basis_overlay_cap.
        ///        Example: 0.25 means overlay applies only when |deviation| >= 25% of cap.
        double basis_overlay_activation_ratio = 0.25;
        /// @brief Overlay multiplier refresh interval. 0 = refresh every bar.
        uint64_t basis_overlay_refresh_ms = 8ull * 60ull * 60ull * 1000ull;
        /// @brief EMA alpha for basis level estimation (0..1).
        double basis_level_ema_alpha = 0.05;
        /// @brief Force carry rebalance when gross exposure deviates from target by this ratio.
        ///        Example: 0.5 means rebalance if gross is outside [50%, 150%] of target.
        ///        Set to a very large value to disable.
        double gross_deviation_trigger_ratio = std::numeric_limits<double>::infinity();
        /// @brief Only apply gross-deviation trigger when per-leg target notional is above this threshold.
        double gross_deviation_trigger_notional_threshold = 50000.0;
        /// @brief Emergency hard cap for per-leg target notional.
        ///        This is a kill-switch only (default disabled). Normal carry sizing should be
        ///        controlled by dynamic ledger/margin/liquidity capacity, not static notional caps.
        double max_leg_notional_usdt = std::numeric_limits<double>::infinity();
        /// @brief Minimum carry sizing scale when confidence is near zero (0..1).
        double carry_confidence_min_scale = 1.0;
        /// @brief Maximum carry sizing scale when confidence is near one (>0).
        double carry_confidence_max_scale = 1.0;
        /// @brief Curvature of confidence scaling; >1 biases toward smaller sizes.
        double carry_confidence_power = 1.25;
        /// @brief Enable core+overlay carry sizing model.
        ///        When enabled, target_notional = base_notional * (core_ratio + overlay_ratio * confidence^power).
        ///        When disabled, fallback to legacy confidence scaling (min/max scale).
        bool carry_core_overlay_enabled = false;
        /// @brief Core notional ratio in core+overlay model (0..1).
        double carry_core_notional_ratio = 0.70;
        /// @brief Overlay notional ratio multiplier in core+overlay model (>=0).
        double carry_overlay_notional_ratio = 0.30;
        /// @brief Confidence curvature in overlay sizing (>0).
        double carry_overlay_confidence_power = 1.0;
        /// @brief Minimum leverage scale for perp legs under low carry confidence (>0).
        double carry_confidence_min_leverage_scale = 1.0;
        /// @brief Maximum leverage scale for perp legs under high carry confidence (>0).
        double carry_confidence_max_leverage_scale = 1.0;
        /// @brief Curvature for leverage scaling by confidence; >1 suppresses high leverage more.
        double carry_confidence_leverage_power = 1.0;
        /// @brief Enable up-only confidence boost on top of base carry sizing.
        bool carry_confidence_boost_enabled = false;
        /// @brief Confidence threshold where boost starts (0..1). Below this, no boost.
        double carry_confidence_boost_reference = 0.60;
        /// @brief Maximum extra sizing scale added at very high confidence (>=0).
        ///        Example: 0.20 means final scale can reach up to +20%.
        double carry_confidence_boost_max_scale = 0.20;
        /// @brief Curvature for confidence boost (>0); >1 delays boost toward very high confidence.
        double carry_confidence_boost_power = 1.0;
        /// @brief Enable funding-structure-aware damping for confidence boost.
        ///        Damping is updated on observed perp funding settlements.
        bool carry_confidence_boost_regime_aware_enabled = false;
        /// @brief Rolling settlement window size for boost regime damping.
        std::size_t carry_confidence_boost_regime_window_settlements = 120;
        /// @brief Minimum settlement samples before regime damping becomes active.
        std::size_t carry_confidence_boost_regime_min_samples = 24;
        /// @brief Penalty weight on negative funding share (0..1).
        ///        effective_boost_max_scale *= max(floor_scale, 1 - weight * negative_share).
        double carry_confidence_boost_regime_negative_share_weight = 0.75;
        /// @brief Lower bound for regime damping scale (0..1).
        double carry_confidence_boost_regime_floor_scale = 0.40;
        /// @brief One-way execution cost rate per leg used in size-change economics gate (e.g., 0.0002 = 2 bps).
        ///        Set <= 0 to disable.
        double carry_size_cost_rate_per_leg = 0.0;
        /// @brief Expected number of future funding settlements used to value a size change.
        ///        Set <= 0 to disable economics gate.
        double carry_size_expected_hold_settlements = 0.0;
        /// @brief Minimum expected gain / expected cost ratio required to apply a size change.
        double carry_size_min_gain_to_cost = 1.0;
        /// @brief Confidence-aware lower-anchor for required gain/cost ratio (applied at confidence=0).
        ///        If equal to high_conf, this reduces to fixed ratio gate.
        double carry_size_min_gain_to_cost_low_confidence = 1.0;
        /// @brief Confidence-aware upper-anchor for required gain/cost ratio (applied at confidence=1).
        ///        Typical usage: low_conf > high_conf to be stricter under weak confidence.
        double carry_size_min_gain_to_cost_high_confidence = 1.0;
        /// @brief Curvature for confidence-aware gain/cost threshold interpolation (>0).
        double carry_size_gain_to_cost_confidence_power = 1.0;
        /// @brief When > 0, carry notional target can scale up to this fraction of total cash.
        ///        Example: 0.20 with 1,000,000 total cash targets 200,000 notional per leg.
        double dual_ledger_auto_notional_ratio = 0.36;
        /// @brief Enable leverage-aware capital allocator for carry sizing.
        ///        target_notional ~= total_cash / (spot_cash_per_notional + 1/leverage + margin_buffer_ratio).
        ///        This models "spot uses more cash, perp uses margin+buffer".
        bool carry_allocator_leverage_model_enabled = false;
        /// @brief Spot cash usage per 1 notional (spot is usually near 1.0).
        double carry_allocator_spot_cash_per_notional = 1.0;
        /// @brief Extra perp margin buffer ratio for liquidation safety (e.g. 0.08 = 8% notional buffer).
        double carry_allocator_perp_margin_buffer_ratio = 0.08;
        /// @brief Override leverage used in allocator formula. <=0 uses current perp leverage setting.
        double carry_allocator_perp_leverage = 0.0;
        /// @brief EMA alpha (0..1) for smoothing auto-notional target from total cash.
        ///        1.0 keeps current behavior (no smoothing), smaller values reduce churn.
        double dual_ledger_auto_notional_ema_alpha = 1.0;
        /// @brief Fraction of spot available cash considered usable for incremental carry sizing.
        double dual_ledger_spot_available_usage = 0.95;
        /// @brief Fraction of perp available balance considered usable for incremental carry sizing.
        double dual_ledger_perp_available_usage = 1.00;
        /// @brief Minimum perp liquidation buffer ratio before carry sizing is throttled.
        ///        buffer_ratio = (MarginBalance - MaintenanceMargin) / max(MaintenanceMargin, 1e-9).
        ///        Set <= 0 to disable this guardrail.
        double perp_liq_buffer_floor_ratio = 0.0;
        /// @brief Buffer ratio where liquidation guard stops throttling (full size).
        ///        Must be > floor ratio to take effect.
        double perp_liq_buffer_ceiling_ratio = 1.0;
        /// @brief Minimum notional scale applied when buffer ratio <= floor.
        double perp_liq_min_notional_scale = 0.25;
        /// @brief Optional explicit instrument typing; when empty, fallback inference is used.
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types;
    };

    explicit SimpleRiskEngine(Config cfg);

    RiskTarget position(const QTrading::Intent::TradeIntent& intent,
        const AccountState& account,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    bool is_spot_instrument_(const std::string& instrument) const;
    bool is_perp_instrument_(const std::string& instrument) const;
    double leverage_for_instrument_(const std::string& instrument) const;
    double leverage_for_instrument_scaled_(const std::string& instrument, double scale) const;

    Config cfg_;
    QTrading::Dto::Trading::InstrumentRegistry instrument_registry_{};
    bool basis_level_ema_initialized_{ false };
    double basis_level_ema_{ 0.0 };
    double basis_level_ema_prev_{ 0.0 };
    bool basis_overlay_multiplier_initialized_{ false };
    double basis_overlay_multiplier_{ 1.0 };
    uint64_t basis_overlay_last_refresh_ts_{ 0 };
    bool neg_basis_scale_active_{ false };
    bool auto_notional_ema_initialized_{ false };
    double auto_notional_ema_{ 0.0 };
    bool has_last_boost_regime_funding_time_{ false };
    uint64_t last_boost_regime_funding_time_{ 0 };
    std::deque<int> boost_regime_funding_sign_history_{};
};

} // namespace QTrading::Risk
