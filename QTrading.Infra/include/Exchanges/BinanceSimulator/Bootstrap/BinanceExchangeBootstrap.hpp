#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"
#include "Exchanges/IExchange.h"

namespace QTrading::Dto::Market::Binance {
class MultiKlineDto;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap {

using MultiKlinePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

std::vector<Contracts::SymbolDataset> ToDatasets(
    const std::vector<std::pair<std::string, std::string>>& symbol_csv);
Contracts::StatusSnapshot BuildInitialStatusSnapshot(
    const Account::AccountInitConfig& init,
    const Config::SimulationConfig& simulation_config);

} // namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap
