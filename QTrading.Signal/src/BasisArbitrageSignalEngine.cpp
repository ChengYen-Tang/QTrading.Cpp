#include "Signal/BasisArbitrageSignalEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace QTrading::Signal {

BasisArbitrageSignalEngine::BasisArbitrageSignalEngine(Config cfg)
    : FundingCarrySignalEngine(cfg)
    , cfg_(std::move(cfg))
{
    cfg_.basis_mr_window_bars = std::max<std::size_t>(cfg_.basis_mr_window_bars, 10);
    cfg_.basis_mr_min_samples = std::max<std::size_t>(cfg_.basis_mr_min_samples, 10);
    cfg_.basis_mr_min_samples = std::min(cfg_.basis_mr_min_samples, cfg_.basis_mr_window_bars);
    if (!std::isfinite(cfg_.basis_mr_entry_z) || cfg_.basis_mr_entry_z < 0.0) {
        cfg_.basis_mr_entry_z = 1.5;
    }
    if (!std::isfinite(cfg_.basis_mr_exit_z) || cfg_.basis_mr_exit_z < 0.0) {
        cfg_.basis_mr_exit_z = 0.6;
    }
    if (cfg_.basis_mr_exit_z > cfg_.basis_mr_entry_z) {
        cfg_.basis_mr_exit_z = cfg_.basis_mr_entry_z;
    }
    if (!std::isfinite(cfg_.basis_mr_max_abs_z) || cfg_.basis_mr_max_abs_z <= 0.0) {
        cfg_.basis_mr_max_abs_z = std::max(6.0, cfg_.basis_mr_entry_z);
    }
    if (cfg_.basis_mr_max_abs_z < cfg_.basis_mr_entry_z) {
        cfg_.basis_mr_max_abs_z = cfg_.basis_mr_entry_z;
    }
    cfg_.basis_mr_entry_persistence_bars =
        std::max(cfg_.basis_mr_entry_persistence_bars, 1u);
    cfg_.basis_mr_exit_persistence_bars =
        std::max(cfg_.basis_mr_exit_persistence_bars, 1u);
    if (!std::isfinite(cfg_.basis_mr_std_floor) || cfg_.basis_mr_std_floor <= 0.0) {
        cfg_.basis_mr_std_floor = 1e-6;
    }
    if (!std::isfinite(cfg_.basis_mr_confidence_floor)) {
        cfg_.basis_mr_confidence_floor = 0.35;
    }
    cfg_.basis_mr_confidence_floor = std::clamp(cfg_.basis_mr_confidence_floor, 0.0, 1.0);

    cfg_.basis_regime_window_bars = std::max<std::size_t>(cfg_.basis_regime_window_bars, 10);
    cfg_.basis_regime_min_samples =
        std::max<std::size_t>(cfg_.basis_regime_min_samples, 10);
    cfg_.basis_regime_min_samples =
        std::min(cfg_.basis_regime_min_samples, cfg_.basis_regime_window_bars);
    if (!std::isfinite(cfg_.basis_regime_calm_z) || cfg_.basis_regime_calm_z < 0.0) {
        cfg_.basis_regime_calm_z = 1.0;
    }
    if (!std::isfinite(cfg_.basis_regime_stress_z) ||
        cfg_.basis_regime_stress_z < cfg_.basis_regime_calm_z)
    {
        cfg_.basis_regime_stress_z = cfg_.basis_regime_calm_z + 1.0;
    }
    if (!std::isfinite(cfg_.basis_regime_min_confidence_scale)) {
        cfg_.basis_regime_min_confidence_scale = 0.5;
    }
    cfg_.basis_regime_min_confidence_scale =
        std::clamp(cfg_.basis_regime_min_confidence_scale, 0.0, 1.0);
}

SignalDecision BasisArbitrageSignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out = FundingCarrySignalEngine::on_market(market);
    out.strategy = "basis_arbitrage";
    std::optional<double> mr_basis_pct_opt = std::nullopt;
    if (cfg_.basis_mr_enabled) {
        mr_basis_pct_opt = ComputeBasisPct(market, cfg_.basis_mr_use_mark_index);
        if (!mr_basis_pct_opt.has_value() || !std::isfinite(*mr_basis_pct_opt)) {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }

        basis_window_.push_back(*mr_basis_pct_opt);
        while (basis_window_.size() > cfg_.basis_mr_window_bars) {
            basis_window_.pop_front();
        }

        if (basis_window_.size() < cfg_.basis_mr_min_samples) {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }

        const double n = static_cast<double>(basis_window_.size());
        const double mean = std::accumulate(basis_window_.begin(), basis_window_.end(), 0.0) / n;
        double var = 0.0;
        for (const double v : basis_window_) {
            const double d = v - mean;
            var += d * d;
        }
        var /= n;
        const double stdv = std::max(std::sqrt(std::max(var, 0.0)), cfg_.basis_mr_std_floor);
        const double z = (*mr_basis_pct_opt - mean) / stdv;
        const double abs_z = std::fabs(z);

        const bool entry_candidate =
            (abs_z >= cfg_.basis_mr_entry_z && abs_z <= cfg_.basis_mr_max_abs_z);
        const bool exit_candidate =
            (abs_z <= cfg_.basis_mr_exit_z || abs_z >= cfg_.basis_mr_max_abs_z);

        if (!mr_active_) {
            const bool cooldown_ok =
                (cfg_.basis_mr_cooldown_ms == 0) ||
                (mr_last_exit_ts_ == 0) ||
                (out.ts_ms >= mr_last_exit_ts_ + cfg_.basis_mr_cooldown_ms);
            if (cooldown_ok && entry_candidate) {
                mr_entry_streak_ += 1;
            }
            else {
                mr_entry_streak_ = 0;
            }
            if (mr_entry_streak_ >= cfg_.basis_mr_entry_persistence_bars) {
                mr_active_ = true;
                mr_entry_streak_ = 0;
                mr_exit_streak_ = 0;
            }
        }
        else {
            if (exit_candidate) {
                mr_exit_streak_ += 1;
            }
            else {
                mr_exit_streak_ = 0;
            }
            if (mr_exit_streak_ >= cfg_.basis_mr_exit_persistence_bars) {
                mr_active_ = false;
                mr_last_exit_ts_ = out.ts_ms;
                mr_exit_streak_ = 0;
                mr_entry_streak_ = 0;
            }
        }

        const bool base_allows_active = (out.status == SignalStatus::Active);
        const bool final_active = base_allows_active && mr_active_;
        out.status = final_active ? SignalStatus::Active : SignalStatus::Inactive;
        if (!final_active) {
            out.confidence = 0.0;
            return out;
        }

        if (cfg_.basis_mr_confidence_from_z) {
            const double span = std::max(cfg_.basis_mr_max_abs_z - cfg_.basis_mr_entry_z, 1e-12);
            const double progress = std::clamp((abs_z - cfg_.basis_mr_entry_z) / span, 0.0, 1.0);
            const double z_scale =
                cfg_.basis_mr_confidence_floor +
                (1.0 - cfg_.basis_mr_confidence_floor) * progress;
            out.confidence = std::clamp(out.confidence * z_scale, 0.0, 1.0);
        }
    }

    if (cfg_.basis_regime_confidence_enabled &&
        out.status == SignalStatus::Active &&
        out.confidence > 0.0)
    {
        auto regime_basis_pct_opt = ComputeBasisPct(market, cfg_.basis_regime_use_mark_index);
        if (regime_basis_pct_opt.has_value() && std::isfinite(*regime_basis_pct_opt)) {
            basis_regime_window_.push_back(*regime_basis_pct_opt);
            while (basis_regime_window_.size() > cfg_.basis_regime_window_bars) {
                basis_regime_window_.pop_front();
            }

            if (basis_regime_window_.size() >= cfg_.basis_regime_min_samples) {
                const double n = static_cast<double>(basis_regime_window_.size());
                const double mean =
                    std::accumulate(basis_regime_window_.begin(), basis_regime_window_.end(), 0.0) /
                    n;
                double var = 0.0;
                for (const double v : basis_regime_window_) {
                    const double d = v - mean;
                    var += d * d;
                }
                var /= n;
                const double stdv = std::max(std::sqrt(std::max(var, 0.0)), cfg_.basis_mr_std_floor);
                const double z = (*regime_basis_pct_opt - mean) / stdv;
                const double abs_z = std::fabs(z);

                double confidence_scale = 1.0;
                if (abs_z > cfg_.basis_regime_calm_z) {
                    const double denom =
                        std::max(cfg_.basis_regime_stress_z - cfg_.basis_regime_calm_z, 1e-12);
                    const double progress =
                        std::clamp((abs_z - cfg_.basis_regime_calm_z) / denom, 0.0, 1.0);
                    confidence_scale =
                        1.0 -
                        (1.0 - cfg_.basis_regime_min_confidence_scale) * progress;
                }
                out.confidence = std::clamp(out.confidence * confidence_scale, 0.0, 1.0);
            }
        }
    }

    return out;
}

bool BasisArbitrageSignalEngine::ResolveSymbolIds(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    if (has_symbol_ids_) {
        return true;
    }
    if (!market || !market->symbols) {
        return false;
    }

    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] == cfg_.spot_symbol) {
            spot_id_ = i;
        }
        if (symbols[i] == cfg_.perp_symbol) {
            perp_id_ = i;
        }
    }

    const bool spot_ok = (spot_id_ < symbols.size() && symbols[spot_id_] == cfg_.spot_symbol);
    const bool perp_ok = (perp_id_ < symbols.size() && symbols[perp_id_] == cfg_.perp_symbol);
    has_symbol_ids_ = spot_ok && perp_ok;
    return has_symbol_ids_;
}

std::optional<double> BasisArbitrageSignalEngine::ComputeBasisPct(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    bool use_mark_index)
{
    if (!ResolveSymbolIds(market)) {
        return std::nullopt;
    }
    if (!market) {
        return std::nullopt;
    }

    if (use_mark_index &&
        perp_id_ < market->mark_klines_by_id.size() &&
        perp_id_ < market->index_klines_by_id.size())
    {
        const auto& mark_opt = market->mark_klines_by_id[perp_id_];
        const auto& index_opt = market->index_klines_by_id[perp_id_];
        if (mark_opt.has_value() && index_opt.has_value() && index_opt->ClosePrice > 0.0) {
            return (mark_opt->ClosePrice - index_opt->ClosePrice) / index_opt->ClosePrice;
        }
    }

    if (spot_id_ >= market->trade_klines_by_id.size() || perp_id_ >= market->trade_klines_by_id.size()) {
        return std::nullopt;
    }

    const auto& spot_opt = market->trade_klines_by_id[spot_id_];
    const auto& perp_opt = market->trade_klines_by_id[perp_id_];
    if (!spot_opt.has_value() || !perp_opt.has_value() || spot_opt->ClosePrice <= 0.0) {
        return std::nullopt;
    }
    return (perp_opt->ClosePrice - spot_opt->ClosePrice) / spot_opt->ClosePrice;
}

} // namespace QTrading::Signal
