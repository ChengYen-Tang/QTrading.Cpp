#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Reduced funding-step action chosen after checking duplicate and mark prerequisites.
enum class FundingDecisionAction : int {
    Apply = 0,
    SkipNoMark = 1,
    SkipDuplicate = 2,
    NoOp = 3,
};

/// Stateless helper that decides whether a funding row should mutate account state.
class FundingEligibilityDecision final {
public:
    /// Chooses the reduced funding action from duplicate/mark/account availability flags.
    static FundingDecisionAction Decide(
        bool is_duplicate,
        bool has_mark_price,
        bool has_account_engine) noexcept
    {
        if (is_duplicate) {
            return FundingDecisionAction::SkipDuplicate;
        }
        if (!has_mark_price) {
            return FundingDecisionAction::SkipNoMark;
        }
        if (has_account_engine) {
            return FundingDecisionAction::Apply;
        }
        return FundingDecisionAction::NoOp;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
