#pragma once

#include <memory>
#include <string>
#include "IIntentBuilder.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Intent {

/// @brief Build a delta-neutral intent for funding carry trades.
/// Design intent:
/// 1) Create a two-leg structure that is delta-neutral (spot + perp).
/// 2) When receive_funding is true, favor the side that collects funding.
/// 3) The intent describes structure only; sizing is handled by the risk engine.
class FundingCarryIntentBuilder final : public IIntentBuilder<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for funding carry intent.
    struct Config {
        std::string spot_symbol;
        std::string perp_symbol;
        bool receive_funding = true;
    };

    explicit FundingCarryIntentBuilder(Config cfg);

    TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
};

} // namespace QTrading::Intent
