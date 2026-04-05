#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/ExecutionOrchestrator.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Execution/LiquidityAwareExecutionScheduler.hpp"
#include "Intent/IIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/ISignalEngine.hpp"
#include "Strategy/FundingCarryStrategyGateway.hpp"
#include "Strategy/IStrategyRuntime.hpp"
#include "Universe/FixedUniverseSelector.hpp"

#include <memory>
#include <unordered_map>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Strategy {

class FundingCarryStrategyRuntime : public IStrategyRuntime {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    FundingCarryStrategyRuntime(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        QTrading::Universe::FixedUniverseSelector& universe_selector,
        QTrading::Signal::ISignalEngine<MarketPtr>& signal_engine,
        QTrading::Intent::IIntentBuilder<MarketPtr>& intent_builder,
        QTrading::Risk::SimpleRiskEngine& risk_engine,
        QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
        QTrading::Monitoring::SimpleMonitoring& monitoring,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    void RunOneCycle() override;

private:
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    QTrading::Universe::FixedUniverseSelector& universe_selector_;
    QTrading::Signal::ISignalEngine<MarketPtr>& signal_engine_;
    QTrading::Intent::IIntentBuilder<MarketPtr>& intent_builder_;
    QTrading::Risk::SimpleRiskEngine& risk_engine_;
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine_;
    QTrading::Monitoring::SimpleMonitoring& monitoring_;
    QTrading::Execution::LiquidityAwareExecutionScheduler execution_scheduler_;
    QTrading::Execution::TargetNotionalExecutionPolicy execution_policy_;
    QTrading::Execution::ExecutionOrchestrator execution_orchestrator_;
    FundingCarryStrategyGateway exchange_gateway_;
};

} // namespace QTrading::Strategy
