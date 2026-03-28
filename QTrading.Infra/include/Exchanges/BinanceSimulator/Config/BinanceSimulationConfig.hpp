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
