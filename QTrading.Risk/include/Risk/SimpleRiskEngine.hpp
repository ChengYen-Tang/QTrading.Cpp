#pragma once

#include <memory>
#include <string>
#include "IRiskEngine.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Risk {

/// @brief Basic risk engine using fixed notional and leverage caps.
class SimpleRiskEngine final : public IRiskEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        double notional_usdt = 1000.0;
        double leverage = 2.0;
        double max_leverage = 3.0;
        /// @brief Skip rebalancing when |net| / gross exposure is below this ratio.
        double rebalance_threshold_ratio = 0.05;
    };

    explicit SimpleRiskEngine(Config cfg);

    RiskTarget position(const QTrading::Intent::TradeIntent& intent,
        const AccountState& account,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
};

} // namespace QTrading::Risk
