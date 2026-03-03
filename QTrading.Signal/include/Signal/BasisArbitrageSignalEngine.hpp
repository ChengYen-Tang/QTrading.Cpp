#pragma once

#include "Signal/FundingCarrySignalEngine.hpp"

namespace QTrading::Signal {

/// @brief Basis-arbitrage signal engine built on top of FundingCarrySignalEngine.
/// This class keeps the same runtime behavior as funding carry for now,
/// but exposes a separate strategy identity for chapter-2 basis research.
class BasisArbitrageSignalEngine : public FundingCarrySignalEngine {
public:
    using Config = FundingCarrySignalEngine::Config;

    explicit BasisArbitrageSignalEngine(Config cfg);

    SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;
};

} // namespace QTrading::Signal
