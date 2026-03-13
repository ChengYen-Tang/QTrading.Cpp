#pragma once

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Execution/FundingCarryStrategy.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"
#include "Universe/FixedUniverseSelector.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace QTrading::Service::Builder {

enum class StrategyProfile {
    FundingCarry,
    BasisArbitrage
};

struct StrategyMetadata {
    std::string strategy_name;
    std::string strategy_profile_param;
    std::filesystem::path config_relative_path;
};

struct StrategyModuleConfigs {
    QTrading::Signal::FundingCarrySignalEngine::Config signal_cfg;
    QTrading::Intent::FundingCarryIntentBuilder::Config intent_cfg;
    QTrading::Risk::SimpleRiskEngine::Config risk_cfg;
    QTrading::Execution::MarketExecutionEngine::Config execution_cfg;
    QTrading::Monitoring::SimpleMonitoring::Config monitoring_cfg;
};

struct StrategyModuleBundle {
    std::unique_ptr<QTrading::Universe::FixedUniverseSelector> universe_selector;
    std::shared_ptr<QTrading::Signal::FundingCarrySignalEngine> signal_engine;
    std::shared_ptr<QTrading::Intent::FundingCarryIntentBuilder> intent_builder;
    std::unique_ptr<QTrading::Risk::SimpleRiskEngine> risk_engine;
    std::unique_ptr<QTrading::Execution::MarketExecutionEngine> execution_engine;
    std::unique_ptr<QTrading::Monitoring::SimpleMonitoring> monitoring;
    std::shared_ptr<QTrading::Execution::FundingCarryStrategy> strategy;
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

} // namespace QTrading::Service::Builder
