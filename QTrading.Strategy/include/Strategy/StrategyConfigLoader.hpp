#pragma once

#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"

#include <filesystem>

namespace QTrading::Strategy {

struct StrategyRuntimeConfig {
    std::size_t basis_multi_top_n = 3;
    std::size_t basis_multi_shard_size = 16;
    std::size_t basis_multi_worker_count = 0;
};

struct StrategyModuleConfigs {
    QTrading::Signal::FundingCarrySignalEngine::Config signal_cfg;
    QTrading::Intent::FundingCarryIntentBuilder::Config intent_cfg;
    QTrading::Risk::SimpleRiskEngine::Config risk_cfg;
    QTrading::Execution::MarketExecutionEngine::Config execution_cfg;
    QTrading::Monitoring::SimpleMonitoring::Config monitoring_cfg;
    StrategyRuntimeConfig runtime_cfg;
};

void LoadFundingCarryConfig(
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs);

void LoadBasisArbitrageConfig(
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs);

} // namespace QTrading::Strategy
