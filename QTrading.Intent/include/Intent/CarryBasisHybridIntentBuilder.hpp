#pragma once

#include "Intent/IIntentBuilder.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"

#include <memory>

namespace QTrading::Intent {

/// @brief Intent builder for carry + basis hybrid.
/// The first executable version intentionally keeps the supported structure as
/// long spot + short USDT-margined perp; spot short is not assumed available.
class CarryBasisHybridIntentBuilder : public IIntentBuilder<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        std::string spot_symbol = "BTCUSDT_SPOT";
        std::string perp_symbol = "BTCUSDT_PERP";
        bool receive_funding = true;
    };

    explicit CarryBasisHybridIntentBuilder(Config cfg);

    TradeIntent build(
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
};

} // namespace QTrading::Intent
