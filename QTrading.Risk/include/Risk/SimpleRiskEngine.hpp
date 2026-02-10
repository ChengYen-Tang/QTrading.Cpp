#pragma once

#include <limits>
#include <memory>
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
        double rebalance_threshold_ratio = 0.01;
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
        /// @brief EMA alpha for basis level estimation (0..1).
        double basis_level_ema_alpha = 0.05;
        /// @brief Force carry rebalance when gross exposure deviates from target by this ratio.
        ///        Example: 0.5 means rebalance if gross is outside [50%, 150%] of target.
        ///        Set to a very large value to disable.
        double gross_deviation_trigger_ratio = std::numeric_limits<double>::infinity();
        /// @brief Only apply gross-deviation trigger when per-leg target notional is above this threshold.
        double gross_deviation_trigger_notional_threshold = 50000.0;
        /// @brief Hard cap for per-leg target notional to avoid unstable churn at oversized capacity.
        double max_leg_notional_usdt = 292000.0;
        /// @brief Minimum carry sizing scale when confidence is near zero (0..1).
        double carry_confidence_min_scale = 1.0;
        /// @brief Maximum carry sizing scale when confidence is near one (>0).
        double carry_confidence_max_scale = 1.0;
        /// @brief Curvature of confidence scaling; >1 biases toward smaller sizes.
        double carry_confidence_power = 1.25;
        /// @brief Minimum leverage scale for perp legs under low carry confidence (>0).
        double carry_confidence_min_leverage_scale = 1.0;
        /// @brief Maximum leverage scale for perp legs under high carry confidence (>0).
        double carry_confidence_max_leverage_scale = 1.0;
        /// @brief Curvature for leverage scaling by confidence; >1 suppresses high leverage more.
        double carry_confidence_leverage_power = 1.0;
        /// @brief When > 0, carry notional target can scale up to this fraction of total cash.
        ///        Example: 0.20 with 1,000,000 total cash targets 200,000 notional per leg.
        double dual_ledger_auto_notional_ratio = 0.292;
        /// @brief EMA alpha (0..1) for smoothing auto-notional target from total cash.
        ///        1.0 keeps current behavior (no smoothing), smaller values reduce churn.
        double dual_ledger_auto_notional_ema_alpha = 1.0;
        /// @brief Fraction of spot available cash considered usable for incremental carry sizing.
        double dual_ledger_spot_available_usage = 0.95;
        /// @brief Fraction of perp available balance considered usable for incremental carry sizing.
        double dual_ledger_perp_available_usage = 1.00;
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
    bool neg_basis_scale_active_{ false };
    bool auto_notional_ema_initialized_{ false };
    double auto_notional_ema_{ 0.0 };
};

} // namespace QTrading::Risk
