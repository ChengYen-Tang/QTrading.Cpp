#pragma once

#include "AccountState.hpp"
#include "RiskTarget.hpp"
#include "Intent/TradeIntent.hpp"

namespace QTrading::Risk {

/// @brief Interface for converting intents into risk-sized targets.
template <typename TMarket>
class IRiskEngine {
public:
    /// @brief Virtual destructor.
    virtual ~IRiskEngine() = default;
    /// @brief Compute target exposures given intent, account, and market snapshot.
    virtual RiskTarget position(const QTrading::Intent::TradeIntent& intent,
        const AccountState& account,
        const TMarket& market) = 0;
};

/// @brief No-op risk engine returning empty targets.
template <typename TMarket>
class NullRiskEngine final : public IRiskEngine<TMarket> {
public:
    RiskTarget position(const QTrading::Intent::TradeIntent&, const AccountState&, const TMarket&) override {
        return RiskTarget{};
    }
};

} // namespace QTrading::Risk
