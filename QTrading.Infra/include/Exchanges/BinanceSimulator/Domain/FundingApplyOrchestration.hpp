#pragma once

#include <utility>

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class FundingApplyOrchestration final {
public:
    template <typename ApplyFundingFn, typename RunNonFundingStagesFn>
    static void Execute(bool funding_before_matching,
        ApplyFundingFn&& apply_funding,
        RunNonFundingStagesFn&& run_non_funding_stages)
    {
        if (funding_before_matching) {
            std::forward<ApplyFundingFn>(apply_funding)();
            std::forward<RunNonFundingStagesFn>(run_non_funding_stages)();
            return;
        }

        std::forward<RunNonFundingStagesFn>(run_non_funding_stages)();
        std::forward<ApplyFundingFn>(apply_funding)();
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
