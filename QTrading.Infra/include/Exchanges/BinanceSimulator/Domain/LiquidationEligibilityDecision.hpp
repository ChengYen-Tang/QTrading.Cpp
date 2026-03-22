#pragma once

#include "Dto/Account/BalanceSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct LiquidationEligibilityDecision {
    bool distressed{ false };
    bool warning_zone{ false };
    bool should_run{ false };
};

class LiquidationEligibility final {
public:
    static LiquidationEligibilityDecision Evaluate(
        const QTrading::Dto::Account::BalanceSnapshot& snapshot,
        double warning_zone_ratio) noexcept
    {
        LiquidationEligibilityDecision out{};
        out.distressed = snapshot.MarginBalance < snapshot.MaintenanceMargin;
        out.warning_zone = (snapshot.MaintenanceMargin > 0.0) &&
            (snapshot.MarginBalance >= snapshot.MaintenanceMargin) &&
            (snapshot.MarginBalance < snapshot.MaintenanceMargin * warning_zone_ratio);
        out.should_run = out.warning_zone || out.distressed;
        return out;
    }

    static bool ShouldContinueStagedStep(
        bool step_distressed,
        bool warning_zone_entered,
        int step) noexcept
    {
        if (step_distressed) {
            return true;
        }
        return warning_zone_entered && step == 0;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
