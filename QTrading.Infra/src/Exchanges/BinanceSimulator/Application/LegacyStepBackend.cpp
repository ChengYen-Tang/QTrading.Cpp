#include "Exchanges/BinanceSimulator/Application/LegacyStepBackend.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

LegacyStepBackend::LegacyStepBackend(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

BinanceExchange::RunStepResult LegacyStepBackend::run_legacy() const
{
    return exchange_.run_legacy_session_step_();
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
