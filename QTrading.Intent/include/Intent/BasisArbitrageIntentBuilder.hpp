#pragma once

#include "Intent/FundingCarryIntentBuilder.hpp"

namespace QTrading::Intent {

/// @brief Basis-arbitrage intent builder.
/// Reuses the proven two-leg carry structure while using basis-specific labels.
class BasisArbitrageIntentBuilder : public FundingCarryIntentBuilder {
public:
    using Config = FundingCarryIntentBuilder::Config;

    explicit BasisArbitrageIntentBuilder(Config cfg);

    TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;
};

} // namespace QTrading::Intent
