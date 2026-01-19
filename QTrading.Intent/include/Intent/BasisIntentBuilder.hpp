#pragma once

#include <memory>
#include <string>
#include "IIntentBuilder.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Intent {

/// @brief Build a delta-neutral intent for basis/funding trades.
class BasisIntentBuilder final : public IIntentBuilder<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        std::string spot_symbol;
        std::string perp_symbol;
    };

    explicit BasisIntentBuilder(Config cfg);

    TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
};

} // namespace QTrading::Intent
