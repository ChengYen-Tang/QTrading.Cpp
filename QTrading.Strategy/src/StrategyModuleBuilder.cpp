#include "Strategy/StrategyModuleBuilder.hpp"

#include "Intent/BasisArbitrageIntentBuilder.hpp"
#include "Signal/BasisArbitrageSignalEngine.hpp"
#include "Strategy/BasisArbitrageMultiPairRuntime.hpp"
#include "Strategy/FundingCarryStrategyRuntime.hpp"
#include "Universe/IUniverseSelector.hpp"

#include <stdexcept>
#include <utility>

namespace QTrading::Strategy {

namespace {

StrategyModuleBundle BuildFundingCarryModules(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    StrategyModuleConfigs configs,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types)
{
    configs.risk_cfg.instrument_types = instrument_types;

    StrategyModuleBundle bundle;
    bundle.universe_selector = std::make_unique<QTrading::Universe::NullUniverseSelector>();
    bundle.signal_engine = std::make_shared<QTrading::Signal::FundingCarrySignalEngine>(configs.signal_cfg);
    bundle.intent_builder = std::make_shared<QTrading::Intent::FundingCarryIntentBuilder>(configs.intent_cfg);
    bundle.risk_engine = std::make_unique<QTrading::Risk::SimpleRiskEngine>(configs.risk_cfg);
    bundle.execution_engine = std::make_unique<QTrading::Execution::MarketExecutionEngine>(exchange, configs.execution_cfg);
    bundle.monitoring = std::make_unique<QTrading::Monitoring::SimpleMonitoring>(configs.monitoring_cfg);
    bundle.strategy = std::make_shared<QTrading::Strategy::FundingCarryStrategyRuntime>(
        exchange,
        *bundle.universe_selector,
        *bundle.signal_engine,
        *bundle.intent_builder,
        *bundle.risk_engine,
        *bundle.execution_engine,
        *bundle.monitoring,
        instrument_types);
    return bundle;
}

StrategyModuleBundle BuildBasisArbitrageModules(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    StrategyModuleConfigs configs,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types)
{
    configs.risk_cfg.instrument_types = instrument_types;
    configs.execution_cfg.carry_require_two_sided_rebalance = true;
    configs.execution_cfg.carry_balance_two_sided_rebalance = true;

    StrategyModuleBundle bundle;
    bundle.universe_selector = std::make_unique<QTrading::Universe::NullUniverseSelector>();
    bundle.risk_engine = std::make_unique<QTrading::Risk::SimpleRiskEngine>(configs.risk_cfg);
    bundle.execution_engine = std::make_unique<QTrading::Execution::MarketExecutionEngine>(exchange, configs.execution_cfg);
    bundle.monitoring = std::make_unique<QTrading::Monitoring::SimpleMonitoring>(configs.monitoring_cfg);
    bundle.strategy = std::make_shared<QTrading::Strategy::BasisArbitrageMultiPairRuntime>(
        exchange,
        *bundle.universe_selector,
        configs.signal_cfg,
        configs.intent_cfg,
        *bundle.risk_engine,
        *bundle.execution_engine,
        *bundle.monitoring,
        instrument_types);
    return bundle;
}

} // namespace

StrategyMetadata GetStrategyMetadata(StrategyProfile profile)
{
    switch (profile) {
    case StrategyProfile::FundingCarry:
        return {
            "FundingCarryMVP",
            "funding_carry_default",
            R"(research/funding_carry/config/funding_carry_v1.json)"
        };
    case StrategyProfile::BasisArbitrage:
        return {
            "BasisArbitrageMVP",
            "basis_arbitrage_default",
            R"(research/basis_arbitrage/config/basis_arbitrage_v1.json)"
        };
    }
    throw std::invalid_argument("Unsupported strategy profile");
}

void LoadStrategyModuleConfigs(
    StrategyProfile profile,
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs)
{
    switch (profile) {
    case StrategyProfile::FundingCarry:
        LoadFundingCarryConfig(config_path, configs);
        return;
    case StrategyProfile::BasisArbitrage:
        LoadBasisArbitrageConfig(config_path, configs);
        return;
    }
    throw std::invalid_argument("Unsupported strategy profile");
}

StrategyModuleBundle BuildStrategyModules(
    StrategyProfile profile,
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    StrategyModuleConfigs configs,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types)
{
    switch (profile) {
    case StrategyProfile::FundingCarry:
        return BuildFundingCarryModules(exchange, std::move(configs), instrument_types);
    case StrategyProfile::BasisArbitrage:
        return BuildBasisArbitrageModules(exchange, std::move(configs), instrument_types);
    }
    throw std::invalid_argument("Unsupported strategy profile");
}

} // namespace QTrading::Strategy
