#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/CarryPairImbalanceCoordinator.hpp"
#include "Execution/FundingCarryExecutionOrchestrator.hpp"
#include "Execution/FundingCarryExchangeGateway.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Execution/IPairCoordinator.hpp"
#include "Execution/LiquidityAwareExecutionScheduler.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"
#include "Universe/FixedUniverseSelector.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Execution {

/// @brief Funding-carry strategy entrypoint used by Service loop.
/// Service drives exchange->step(), then waits strategy to consume that tick.
class FundingCarryStrategy final {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    FundingCarryStrategy(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        QTrading::Universe::FixedUniverseSelector& universe_selector,
        QTrading::Signal::FundingCarrySignalEngine& signal_engine,
        QTrading::Intent::FundingCarryIntentBuilder& intent_builder,
        QTrading::Risk::SimpleRiskEngine& risk_engine,
        QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
        QTrading::Monitoring::SimpleMonitoring& monitoring,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    void wait_for_done();

private:
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    QTrading::Universe::FixedUniverseSelector& universe_selector_;
    QTrading::Signal::FundingCarrySignalEngine& signal_engine_;
    QTrading::Intent::FundingCarryIntentBuilder& intent_builder_;
    QTrading::Risk::SimpleRiskEngine& risk_engine_;
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine_;
    QTrading::Monitoring::SimpleMonitoring& monitoring_;
    QTrading::Execution::LiquidityAwareExecutionScheduler execution_scheduler_;
    QTrading::Execution::TargetNotionalExecutionPolicy execution_policy_;
    QTrading::Execution::CarryPairImbalanceCoordinator pair_coordinator_;
    QTrading::Execution::FundingCarryExecutionOrchestrator execution_orchestrator_;
    QTrading::Execution::FundingCarryExchangeGateway exchange_gateway_;
};

} // namespace QTrading::Execution
