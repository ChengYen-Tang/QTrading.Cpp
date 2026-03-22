#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

class LegacyStepBackend final {
public:
    explicit LegacyStepBackend(BinanceExchange& exchange) noexcept;

    BinanceExchange::RunStepResult run_legacy() const;

private:
    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
