#pragma once

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Config {

struct SimulationConfig {
    Contracts::FundingApplyTiming funding_apply_timing{ Contracts::FundingApplyTiming::BeforeMatching };
    double uncertainty_band_bps{ 0.0 };
    double basis_warning_bps{ 0.0 };
    double basis_stress_bps{ 0.0 };
    double basis_warning_cap{ 0.0 };
    double basis_stress_cap{ 0.0 };
    bool simulator_risk_overlay_enabled{ false };
    bool basis_risk_guard_enabled{ false };
    bool basis_stress_blocks_opening_orders{ false };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Config
