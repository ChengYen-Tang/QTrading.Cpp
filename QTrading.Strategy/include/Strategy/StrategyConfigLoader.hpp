#pragma once

#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"

#include <filesystem>
#include <unordered_set>

namespace QTrading::Strategy {

struct StrategyRuntimeConfig {
    std::size_t basis_multi_top_n = 3;
    std::size_t basis_multi_shard_size = 16;
    std::size_t basis_multi_worker_count = 0;
    double basis_multi_min_score_ratio = 0.35;
    double basis_multi_confidence_power = 1.0;
    double basis_multi_max_pair_weight = 0.60;
    double basis_multi_min_effective_quality_scale = 0.0;
    double basis_multi_min_effective_allocator_score = 0.0;
    double basis_pair_min_spot_quote_volume = 0.0;
    double basis_pair_min_perp_quote_volume = 0.0;
    double basis_pair_min_quote_volume_ratio = 0.0;
    bool basis_quality_enabled = false;
    std::size_t basis_quality_window_bars = 240;
    std::size_t basis_quality_min_samples = 120;
    double basis_quality_min_abs_basis_p95_pct = 0.0;
    double basis_quality_max_spot_zero_volume_share = 1.0;
    double basis_quality_min_spot_perp_quote_ratio = 0.0;
    std::size_t basis_quality_structural_min_samples = 0;
    double basis_quality_structural_min_abs_basis_mean_pct = 0.0;
    double basis_quality_structural_max_spot_zero_volume_share = 1.0;
    double basis_quality_structural_max_spot_perp_quote_ratio = 0.0;
    double basis_quality_structural_exception_max_spot_zero_volume_share = 1.0;
    double basis_quality_structural_exception_min_abs_basis_mean_pct = 0.0;
    double basis_quality_structural_exception_max_spot_perp_quote_ratio = 0.0;
    std::unordered_set<std::string> basis_allowed_raw_symbols;
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
