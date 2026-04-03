#pragma once

#include <vector>

#include "Exchanges/BinanceSimulator/Account/Config.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

double ComputeMaintenanceMargin(double notional) noexcept;
double ComputeMaintenanceMargin(double notional, const std::vector<MarginTier>& tiers) noexcept;
double ComputeMaintenanceMarginForSymbol(
    double notional,
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept;

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
