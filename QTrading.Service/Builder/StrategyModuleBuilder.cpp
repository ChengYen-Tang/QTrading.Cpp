#include "StrategyModuleBuilder.hpp"

#include "Intent/BasisArbitrageIntentBuilder.hpp"
#include "Signal/BasisArbitrageSignalEngine.hpp"
#include "../ServiceHelpers.hpp"

#include <stdexcept>
#include <utility>

namespace QTrading::Service::Builder {

namespace {

StrategyModuleBundle BuildFundingCarryModules(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    StrategyModuleConfigs configs,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types)
{
    // Keep FundingCarry wiring aligned with master branch composition.
    configs.risk_cfg.instrument_types = instrument_types;

    StrategyModuleBundle bundle;
    bundle.universe_selector = std::make_unique<QTrading::Universe::FixedUniverseSelector>();
    bundle.signal_engine = std::make_shared<QTrading::Signal::FundingCarrySignalEngine>(configs.signal_cfg);
    bundle.intent_builder = std::make_shared<QTrading::Intent::FundingCarryIntentBuilder>(configs.intent_cfg);
    bundle.risk_engine = std::make_unique<QTrading::Risk::SimpleRiskEngine>(configs.risk_cfg);
    bundle.execution_engine = std::make_unique<QTrading::Execution::MarketExecutionEngine>(exchange, configs.execution_cfg);
    bundle.monitoring = std::make_unique<QTrading::Monitoring::SimpleMonitoring>(configs.monitoring_cfg);
    bundle.strategy = std::make_shared<QTrading::Execution::FundingCarryStrategy>(
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
    bundle.universe_selector = std::make_unique<QTrading::Universe::FixedUniverseSelector>();
    bundle.signal_engine = std::make_shared<QTrading::Signal::BasisArbitrageSignalEngine>(configs.signal_cfg);
    bundle.intent_builder = std::make_shared<QTrading::Intent::BasisArbitrageIntentBuilder>(configs.intent_cfg);
    bundle.risk_engine = std::make_unique<QTrading::Risk::SimpleRiskEngine>(configs.risk_cfg);
    bundle.execution_engine = std::make_unique<QTrading::Execution::MarketExecutionEngine>(exchange, configs.execution_cfg);
    bundle.monitoring = std::make_unique<QTrading::Monitoring::SimpleMonitoring>(configs.monitoring_cfg);
    bundle.strategy = std::make_shared<QTrading::Execution::FundingCarryStrategy>(
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
        QTrading::Service::Helpers::LoadFundingCarryConfig(
            config_path,
            configs.signal_cfg,
            configs.intent_cfg,
            configs.risk_cfg,
            configs.execution_cfg,
            configs.monitoring_cfg);
        return;
    case StrategyProfile::BasisArbitrage:
        QTrading::Service::Helpers::LoadBasisArbitrageConfig(
            config_path,
            configs.signal_cfg,
            configs.intent_cfg,
            configs.risk_cfg,
            configs.execution_cfg,
            configs.monitoring_cfg);
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

} // namespace QTrading::Service::Builder
