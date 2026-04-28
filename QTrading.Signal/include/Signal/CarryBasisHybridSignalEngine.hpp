#pragma once

#include "Signal/BasisArbitrageSignalEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"
#include "Signal/ISignalEngine.hpp"
#include "Signal/PairMarketSignalSupport.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace QTrading::Signal {

/// @brief Hybrid signal for funding-carry core with basis-arbitrage overlay.
/// Funding carry remains the primary holding permission; basis MR controls
/// confidence/size rather than replacing the carry thesis.
class CarryBasisHybridSignalEngine : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        FundingCarrySignalEngine::Config funding_cfg;
        BasisArbitrageSignalEngine::Config basis_cfg;
        bool require_funding_active = true;
        bool allow_basis_only_entry = false;
        bool basis_overlay_enabled = true;
        double funding_confidence_weight = 0.70;
        double basis_confidence_weight = 0.30;
        double basis_inactive_confidence_scale = 0.55;
        double basis_active_boost_scale = 1.10;
        double min_active_confidence = 0.05;
        bool funding_regime_filter_enabled = false;
        std::size_t funding_regime_window_settlements = 90;
        std::size_t funding_regime_min_samples = 24;
        double funding_regime_min_mean_rate = 0.0;
        double funding_regime_max_negative_share = 0.45;
        double funding_regime_confidence_floor = 0.20;
        bool funding_allocator_score_enabled = true;
        double funding_allocator_reference_rate = 0.00010;
        double funding_allocator_weight = 1.0;
        double basis_allocator_weight = 0.25;
    };

    explicit CarryBasisHybridSignalEngine(Config cfg);

    SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    FundingCarrySignalEngine funding_signal_;
    BasisArbitrageSignalEngine basis_signal_;
    QTrading::Signal::Support::PairSymbolIds symbol_ids_{};
    std::uint64_t last_funding_time_{ 0 };
    bool has_last_funding_time_{ false };
    std::deque<double> funding_regime_rates_{};
    bool funding_regime_cached_allows_entry_{ true };
    double funding_regime_cached_confidence_scale_{ 1.0 };

    bool funding_regime_allows_entry(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
        double& confidence_scale);
};

} // namespace QTrading::Signal
