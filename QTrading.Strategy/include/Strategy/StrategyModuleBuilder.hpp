#pragma once

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/IIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/ISignalEngine.hpp"
#include "Strategy/IStrategyRuntime.hpp"
#include "Strategy/StrategyConfigLoader.hpp"
#include "Universe/IUniverseSelector.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace QTrading::Strategy {

enum class StrategyProfile {
    FundingCarry,
    BasisArbitrage
};

struct StrategyMetadata {
    std::string strategy_name;
    std::string strategy_profile_param;
    std::filesystem::path config_relative_path;
};

struct StrategyModuleBundle {
    std::unique_ptr<QTrading::Universe::IUniverseSelector> universe_selector;
    std::shared_ptr<QTrading::Signal::ISignalEngine<std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> signal_engine;
    std::shared_ptr<QTrading::Intent::IIntentBuilder<std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> intent_builder;
    std::unique_ptr<QTrading::Risk::SimpleRiskEngine> risk_engine;
    std::unique_ptr<QTrading::Execution::MarketExecutionEngine> execution_engine;
    std::unique_ptr<QTrading::Monitoring::SimpleMonitoring> monitoring;
    std::shared_ptr<QTrading::Strategy::IStrategyRuntime> strategy;
};

StrategyMetadata GetStrategyMetadata(StrategyProfile profile);

void LoadStrategyModuleConfigs(
    StrategyProfile profile,
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs);

StrategyModuleBundle BuildStrategyModules(
    StrategyProfile profile,
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    StrategyModuleConfigs configs,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types);

} // namespace QTrading::Strategy
