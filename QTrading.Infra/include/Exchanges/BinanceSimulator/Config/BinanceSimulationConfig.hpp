#pragma once

#include <cstdint>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Config {

enum class SpotCommissionMode {
    QuoteOnBuyQuoteOnSell = 0,
    BaseOnBuyQuoteOnSell = 1,
};

enum class IntraBarPathMode {
    CloseMarketability = 0,
    OpenMarketability = 1,
    MonteCarloPath = 2,
};

enum class KlineVolumeSplitMode {
    TotalOnly = 0,
    OppositePassiveSplit = 1,
};

struct SimulationConfig {
    SpotCommissionMode spot_commission_mode{ SpotCommissionMode::QuoteOnBuyQuoteOnSell };
    Contracts::FundingApplyTiming funding_apply_timing{ Contracts::FundingApplyTiming::BeforeMatching };
    IntraBarPathMode intra_bar_path_mode{ IntraBarPathMode::CloseMarketability };
    KlineVolumeSplitMode kline_volume_split_mode{ KlineVolumeSplitMode::TotalOnly };
    uint64_t intra_bar_random_seed{ 42ull };
    uint32_t intra_bar_monte_carlo_samples{ 1u };
    bool limit_fill_probability_enabled{ false };
    double limit_fill_probability_bias{ 0.0 };
    double limit_fill_probability_penetration_weight{ 0.0 };
    double limit_fill_probability_size_weight{ 0.0 };
    double limit_fill_probability_taker_weight{ 0.0 };
    double limit_fill_probability_interaction_weight{ 0.0 };
    bool taker_probability_model_enabled{ false };
    double taker_probability_bias{ 0.0 };
    double taker_probability_penetration_weight{ 0.0 };
    double taker_probability_size_weight{ 0.0 };
    double taker_probability_taker_weight{ 0.0 };
    double taker_probability_interaction_weight{ 0.0 };
    double market_execution_slippage{ 0.0 };
    double limit_execution_slippage{ 0.0 };
    bool market_impact_slippage_enabled{ false };
    double market_impact_base_bps{ 0.0 };
    double market_impact_max_bps{ 0.0 };
    double market_impact_size_exponent{ 1.0 };
    double market_impact_liquidity_bias{ 0.0 };
    double market_impact_offset_bps{ 0.0 };
    double uncertainty_band_bps{ 0.0 };
    double basis_warning_bps{ 0.0 };
    // Reserved risk-overlay controls kept for facade/config compatibility.
    // Current kernel does not activate stress/opening-block/leverage-cap logic.
    double basis_stress_bps{ 0.0 };
    double basis_warning_cap{ 0.0 };
    double basis_stress_cap{ 0.0 };
    bool simulator_risk_overlay_enabled{ false };
    bool basis_risk_guard_enabled{ false };
    bool basis_stress_blocks_opening_orders{ false };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Config
