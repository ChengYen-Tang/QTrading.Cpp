#include "Exchanges/BinanceSimulator/Domain/MaintenanceMarginModel.hpp"

#include <algorithm>

#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

const std::vector<MarginTier>& resolve_symbol_tiers(
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept
{
    if (symbol_id < step_state.symbol_maintenance_margin_tiers_by_id.size()) {
        const auto& symbol_tiers = step_state.symbol_maintenance_margin_tiers_by_id[symbol_id];
        if (!symbol_tiers.empty()) {
            return symbol_tiers;
        }
    }
    return margin_tiers;
}

} // namespace

double ComputeMaintenanceMargin(double notional, const std::vector<MarginTier>& tiers) noexcept
{
    if (!(notional > 0.0) || tiers.empty()) {
        return 0.0;
    }

    double maintenance = 0.0;
    double lower = 0.0;
    const double target = notional;
    for (const auto& tier : tiers) {
        const double upper = std::max(lower, tier.notional_upper);
        if (!(upper > lower)) {
            continue;
        }
        const double clipped_upper = std::min(target, upper);
        if (clipped_upper > lower) {
            maintenance += (clipped_upper - lower) * tier.maintenance_margin_rate;
            if (clipped_upper >= target) {
                return maintenance;
            }
        }
        lower = upper;
    }

    return maintenance;
}

double ComputeMaintenanceMargin(double notional) noexcept
{
    return ComputeMaintenanceMargin(notional, margin_tiers);
}

double ComputeMaintenanceMarginForSymbol(
    double notional,
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept
{
    return ComputeMaintenanceMargin(notional, resolve_symbol_tiers(step_state, symbol_id));
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
