#pragma once

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap {
/// Builds the initial status snapshot used before the first successful step.
Contracts::StatusSnapshot BuildInitialStatusSnapshot(
    const Account::AccountInitConfig& init,
    const Config::SimulationConfig& simulation_config);

} // namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap
