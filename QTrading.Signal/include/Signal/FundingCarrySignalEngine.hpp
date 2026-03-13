#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include "ISignalEngine.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Signal {

/// @brief Funding carry signal (always-on delta-neutral carry stance).
/// Design intent:
/// 1) Funding carry is a position-based strategy, not a timing strategy.
/// 2) The signal validates market readiness (spot + perp data available).
/// 3) Guardrails (funding/basis thresholds) prevent holding during unfavorable regimes.
/// 4) Execution urgency stays low; funding is earned over time, not a single tick.
class FundingCarrySignalEngine : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for funding carry signal.
    struct Config {
        std::string spot_symbol = "BTCUSDT_SPOT";
        std::string perp_symbol = "BTCUSDT_PERP";
        double entry_min_funding_rate = 0.0;
        double exit_min_funding_rate = 0.0;
        /// @brief Emergency funding floor; when current observed funding <= this threshold,
        ///        position exits immediately (ignores min_hold). Set <= -1 to disable.
        double hard_negative_funding_rate = -1.0;
        double entry_max_basis_pct = 1.0;
        double exit_max_basis_pct = 1.0;
        uint64_t cooldown_ms = 0;
        /// @brief Minimum holding time once entered (ms).
        uint64_t min_hold_ms = 8ull * 60ull * 60ull * 1000ull;
        /// @brief Require this many consecutive funding settlements >= entry threshold before entry.
        uint32_t entry_persistence_settlements = 1;
        /// @brief Require this many consecutive funding settlements < exit threshold before exit.
        uint32_t exit_persistence_settlements = 1;
        /// @brief If true, update funding EMA only when observed funding settlement timestamp changes.
        bool lock_funding_to_settlement = true;
        /// @brief Max allowed age for observed funding snapshot; older data falls back to basis proxy.
        uint64_t observed_funding_max_age_ms = 12ull * 60ull * 60ull * 1000ull;
        /// @brief Enable adaptive statistical gates from rolling histories.
        bool adaptive_gate_enabled = false;
        /// @brief Rolling window size for observed funding settlements.
        std::size_t adaptive_funding_window_settlements = 180;
        /// @brief Minimum settlement samples before adaptive funding gate activates.
        std::size_t adaptive_funding_min_samples = 24;
        /// @brief Funding quantile for entry threshold (receive-funding mode should be stricter).
        double adaptive_funding_entry_quantile = 0.60;
        /// @brief Funding quantile for exit threshold (should be <= entry quantile for hysteresis).
        double adaptive_funding_exit_quantile = 0.40;
        /// @brief Rolling window size for basis |pct| samples.
        std::size_t adaptive_basis_window_bars = 10080; // ~7 days on 1m bars.
        /// @brief Minimum basis samples before adaptive basis gate activates.
        std::size_t adaptive_basis_min_samples = 1440; // ~1 day on 1m bars.
        /// @brief Basis |pct| quantile used as entry max threshold.
        double adaptive_basis_entry_quantile = 0.70;
        /// @brief Basis |pct| quantile used as exit max threshold.
        double adaptive_basis_exit_quantile = 0.85;
        /// @brief Lower floor for adaptive basis threshold to avoid over-tight micro bands.
        double adaptive_basis_floor_pct = 0.0025;
        /// @brief Recompute adaptive basis quantiles at this interval (ms), 0 = every bar.
        /// Default uses 8h cadence to match funding settlement rhythm and avoid overfitting to micro noise.
        uint64_t adaptive_basis_refresh_ms = 8ull * 60ull * 60ull * 1000ull;
        /// @brief Enable soft funding gate from rolling quantiles with floor/cap clamping.
        ///        This avoids long no-trade periods under fixed high thresholds.
        bool adaptive_funding_soft_gate_enabled = false;
        /// @brief Lower floor for soft entry funding threshold.
        double adaptive_funding_entry_floor_rate = 0.00001;
        /// @brief Upper cap for soft entry funding threshold.
        double adaptive_funding_entry_cap_rate = 0.00008;
        /// @brief Exit threshold ratio from soft entry threshold (hysteresis).
        double adaptive_funding_exit_ratio = 0.30;
        /// @brief If inactive for this many observed settlement updates, allow watchdog entry.
        ///        Default is enabled to prevent multi-month no-trade dead zones when gates are too strict.
        ///        Set to 0 to disable.
        uint32_t inactivity_watchdog_settlements = 6;
        /// @brief Minimum observed funding required for watchdog entry.
        double inactivity_watchdog_min_rate = 0.0;
        /// @brief Minimum confidence applied when entry is triggered by inactivity watchdog.
        double inactivity_watchdog_min_confidence = 0.35;
        /// @brief Consecutive settlement count required for hard-negative emergency exit.
        ///        Default 1 preserves previous immediate-exit semantics.
        uint32_t hard_negative_persistence_settlements = 1;
        /// @brief Enable regime-aware persistence adaptation based on settlement sign persistence.
        bool adaptive_regime_enabled = false;
        /// @brief Minimum settlement samples required before adaptive regime persistence is active.
        std::size_t adaptive_regime_min_samples = 60;
        /// @brief Rolling window size for settlement sign persistence estimation.
        std::size_t adaptive_regime_sign_window_settlements = 120;
        /// @brief High sign-persistence threshold (same-sign ratio) for trending regime.
        double adaptive_regime_sign_persist_high = 0.88;
        /// @brief Low sign-persistence threshold (same-sign ratio) for choppy regime.
        double adaptive_regime_sign_persist_low = 0.75;
        /// @brief Entry persistence in trending regime (usually smaller).
        uint32_t adaptive_regime_entry_persistence_low = 1;
        /// @brief Entry persistence in neutral regime.
        uint32_t adaptive_regime_entry_persistence_mid = 2;
        /// @brief Entry persistence in choppy regime (usually larger).
        uint32_t adaptive_regime_entry_persistence_high = 3;
        /// @brief Exit persistence in trending regime (usually larger).
        uint32_t adaptive_regime_exit_persistence_low = 2;
        /// @brief Exit persistence in neutral regime.
        uint32_t adaptive_regime_exit_persistence_mid = 2;
        /// @brief Exit persistence in choppy regime (usually smaller).
        uint32_t adaptive_regime_exit_persistence_high = 1;
        /// @brief Enable settlement-statistics-based confidence scaling (size modulation, not gating).
        bool adaptive_confidence_enabled = true;
        /// @brief Minimum settlement samples before adaptive confidence activates.
        std::size_t adaptive_confidence_min_samples = 24;
        /// @brief Lower funding quantile used as weak-regime anchor.
        double adaptive_confidence_low_quantile = 0.35;
        /// @brief Upper funding quantile used as strong-regime anchor.
        double adaptive_confidence_high_quantile = 0.75;
        /// @brief Minimum confidence multiplier in weak funding regimes (0..1).
        double adaptive_confidence_floor = 0.30;
        /// @brief Maximum confidence multiplier in strong funding regimes (>= floor).
        double adaptive_confidence_ceiling = 1.00;
        /// @brief EMA alpha for smoothing confidence multiplier updates on settlements.
        double adaptive_confidence_ema_alpha = 0.35;
        /// @brief Quantization step for confidence multiplier to reduce rebalance churn. Set <= 0 to disable.
        double adaptive_confidence_bucket_step = 0.05;
        /// @brief Enable funding-structure-aware confidence scaling from negative-share / negative-run features.
        bool adaptive_structure_enabled = true;
        /// @brief Minimum settlement samples required before adaptive structure scaling activates.
        std::size_t adaptive_structure_min_samples = 24;
        /// @brief Penalty weight applied to negative funding share (0..1) in structure score.
        double adaptive_structure_neg_share_weight = 0.75;
        /// @brief Penalty weight applied to normalized trailing negative-run length in structure score.
        double adaptive_structure_neg_run_weight = 0.35;
        /// @brief Normalization factor for trailing negative-run length (in settlements).
        double adaptive_structure_neg_run_norm = 12.0;
        /// @brief Minimum structure multiplier under hostile funding structure (0..1).
        double adaptive_structure_floor = 0.30;
        /// @brief Maximum structure multiplier under favorable funding structure (>= floor).
        double adaptive_structure_ceiling = 1.00;
        /// @brief EMA alpha for smoothing structure multiplier updates on settlements.
        double adaptive_structure_ema_alpha = 0.25;
        /// @brief Quantization step for structure multiplier to reduce frequent tiny changes. Set <= 0 to disable.
        double adaptive_structure_bucket_step = 0.05;
        /// @brief Enable linear funding nowcast between settlement snapshots.
        ///        nowcast(t) = funding_settlement + progress * (basis_proxy - funding_settlement).
        bool funding_nowcast_enabled = false;
        /// @brief Funding settlement interval used by nowcast progress.
        uint64_t funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
        /// @brief If true, funding gate streak logic uses nowcast continuously instead of only settlement updates.
        bool funding_nowcast_use_for_gates = false;
        /// @brief If true, entry streak gate uses nowcast continuously.
        ///        This can improve entry responsiveness while keeping exit gate settlement-based.
        bool funding_nowcast_use_for_entry_gate = false;
        /// @brief If true, exit streak gate uses nowcast continuously.
        ///        Keep false if you want slower, settlement-confirmed exits.
        bool funding_nowcast_use_for_exit_gate = false;
        /// @brief If true, confidence/EMA path uses nowcast continuously.
        ///        Keep false to avoid confidence whipsaw from minute-level nowcast noise.
        bool funding_nowcast_use_for_confidence = false;
        /// @brief Minimum sampling interval for nowcast gate streak updates (ms).
        ///        Applies only when entry/exit gate uses nowcast. Set 0 to evaluate every bar.
        uint64_t funding_nowcast_gate_sample_ms = 60ull * 60ull * 1000ull; // 1h
        /// @brief Enable emergency exit shortly before next settlement when projected next funding is deeply negative.
        bool pre_settlement_negative_exit_enabled = false;
        /// @brief Projected-next funding threshold to trigger pre-settlement emergency exit.
        ///        Set <= -1 to disable threshold check.
        double pre_settlement_negative_exit_threshold = -1.0;
        /// @brief Lookahead window before next settlement (ms) where emergency pre-settlement exit can trigger.
        uint64_t pre_settlement_negative_exit_lookahead_ms = 60ull * 60ull * 1000ull; // 1h
        /// @brief Additional re-entry block after projected settlement time when pre-settlement exit triggers.
        ///        0 means re-entry allowed right after projected settlement boundary.
        uint64_t pre_settlement_negative_exit_reentry_buffer_ms = 0;
        /// @brief If true, pre-settlement emergency exit only applies when funding gate is enabled.
        ///        This prevents accidental churn in always-on (no funding gate) configurations.
        bool pre_settlement_negative_exit_require_funding_gate = true;
        /// @brief Mark-index basis soft-derisk start threshold (bps). <=0 disables soft derisk.
        double mark_index_soft_derisk_start_bps = 0.0;
        /// @brief Mark-index basis soft-derisk full threshold (bps). Must be >= start threshold.
        double mark_index_soft_derisk_full_bps = 0.0;
        /// @brief Minimum confidence multiplier under full soft-derisk pressure (0..1].
        double mark_index_soft_derisk_min_confidence_scale = 0.30;
        /// @brief Mark-index basis hard-exit threshold (bps). <=0 disables hard guard.
        double mark_index_hard_exit_bps = 0.0;
    };

    explicit FundingCarrySignalEngine(Config cfg);

    /// @brief Update signal based on latest market snapshot.
    virtual SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    bool has_symbol_ids_{ false };
    std::size_t spot_id_{ 0 };
    std::size_t perp_id_{ 0 };
    bool active_{ false };
    uint64_t last_exit_ts_{ 0 };
    uint64_t pre_settlement_reentry_block_until_ts_{ 0 };
    uint64_t active_since_ts_{ 0 };
    uint64_t last_observed_funding_time_{ 0 };
    bool has_last_observed_funding_time_{ false };
    bool funding_proxy_initialized_{ false };
    double funding_proxy_ema_{ 0.0 };
    uint32_t funding_entry_good_streak_{ 0 };
    uint32_t funding_exit_bad_streak_{ 0 };
    uint32_t funding_hard_negative_streak_{ 0 };
    uint32_t inactive_settlement_streak_{ 0 };
    std::deque<double> funding_settlement_history_{};
    std::deque<double> basis_abs_history_{};
    bool adaptive_funding_thresholds_ready_{ false };
    double adaptive_funding_entry_min_cached_rate_{ 0.0 };
    double adaptive_funding_exit_min_cached_rate_{ 0.0 };
    std::deque<int> funding_settlement_sign_history_{};
    bool adaptive_regime_ready_{ false };
    uint32_t adaptive_regime_entry_persistence_cached_{ 1 };
    uint32_t adaptive_regime_exit_persistence_cached_{ 1 };
    bool adaptive_confidence_ready_{ false };
    double adaptive_confidence_multiplier_{ 1.0 };
    bool adaptive_structure_ready_{ false };
    double adaptive_structure_multiplier_{ 1.0 };
    bool adaptive_basis_thresholds_ready_{ false };
    uint64_t adaptive_basis_last_refresh_ts_{ 0 };
    double adaptive_basis_entry_max_cached_pct_{ 1.0 };
    double adaptive_basis_exit_max_cached_pct_{ 1.0 };
    bool has_last_nowcast_entry_gate_eval_ts_{ false };
    bool has_last_nowcast_exit_gate_eval_ts_{ false };
    uint64_t last_nowcast_entry_gate_eval_ts_{ 0 };
    uint64_t last_nowcast_exit_gate_eval_ts_{ 0 };

    bool market_has_symbols(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market);
};

} // namespace QTrading::Signal
