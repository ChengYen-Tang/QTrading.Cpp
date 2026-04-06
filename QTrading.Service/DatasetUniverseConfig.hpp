#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace QTrading::Service {

struct SimulatorConfig {
    std::vector<std::string> symbols;
};

SimulatorConfig LoadSimulatorConfig(const std::filesystem::path& config_path);

std::vector<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::SymbolDataset>
BuildSymbolDatasets(const std::vector<std::string>& raw_symbols);

} // namespace QTrading::Service
