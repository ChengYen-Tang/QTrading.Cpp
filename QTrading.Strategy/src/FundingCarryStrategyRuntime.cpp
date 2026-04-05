#include "Strategy/FundingCarryStrategyRuntime.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Signal/SignalDecision.hpp"

namespace {

QTrading::Execution::ExecutionSignalStatus ToExecutionStatus(
    QTrading::Signal::SignalStatus status)
{
    switch (status) {
    case QTrading::Signal::SignalStatus::Active:
        return QTrading::Execution::ExecutionSignalStatus::Active;
    case QTrading::Signal::SignalStatus::Cooldown:
        return QTrading::Execution::ExecutionSignalStatus::Cooldown;
    case QTrading::Signal::SignalStatus::Inactive:
    default:
        return QTrading::Execution::ExecutionSignalStatus::Inactive;
    }
}

QTrading::Execution::ExecutionUrgency ToExecutionUrgency(
    QTrading::Signal::SignalUrgency urgency)
{
    switch (urgency) {
    case QTrading::Signal::SignalUrgency::High:
        return QTrading::Execution::ExecutionUrgency::High;
    case QTrading::Signal::SignalUrgency::Medium:
        return QTrading::Execution::ExecutionUrgency::Medium;
    case QTrading::Signal::SignalUrgency::Low:
    default:
        return QTrading::Execution::ExecutionUrgency::Low;
    }
}

QTrading::Execution::ExecutionSignal ToExecutionSignal(
    const QTrading::Signal::SignalDecision& signal)
{
    return QTrading::Execution::ExecutionSignal{
        signal.ts_ms,
        signal.symbol,
        signal.strategy,
        signal.strategy_kind,
        ToExecutionStatus(signal.status),
        signal.confidence,
        ToExecutionUrgency(signal.urgency)
    };
}

} // namespace

namespace QTrading::Strategy {

FundingCarryStrategyRuntime::FundingCarryStrategyRuntime(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    QTrading::Universe::FixedUniverseSelector& universe_selector,
    QTrading::Signal::ISignalEngine<MarketPtr>& signal_engine,
    QTrading::Intent::IIntentBuilder<MarketPtr>& intent_builder,
    QTrading::Risk::SimpleRiskEngine& risk_engine,
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
    QTrading::Monitoring::SimpleMonitoring& monitoring,
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types)
    : exchange_(std::move(exchange))
    , universe_selector_(universe_selector)
    , signal_engine_(signal_engine)
    , intent_builder_(intent_builder)
    , risk_engine_(risk_engine)
    , execution_engine_(execution_engine)
    , monitoring_(monitoring)
    , execution_scheduler_()
    , execution_policy_()
    , execution_orchestrator_(execution_engine_, execution_scheduler_, execution_policy_)
    , exchange_gateway_(exchange_, std::move(instrument_types))
{
}

void FundingCarryStrategyRuntime::RunOneCycle()
{
    auto market_opt = exchange_->get_market_channel()->Receive();
    if (!market_opt.has_value()) {
        return;
    }
    const auto& market = market_opt.value();

    (void)universe_selector_.select();
    const auto signal = signal_engine_.on_market(market);
    const auto execution_signal = ToExecutionSignal(signal);
    const auto intent = intent_builder_.build(signal, market);
    const auto account = exchange_gateway_.BuildAccountState();
    const auto risk = risk_engine_.position(intent, account, market);
    const auto orders = execution_orchestrator_.Execute(risk, account, execution_signal, market);
    exchange_gateway_.SubmitOrders(orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Strategy
