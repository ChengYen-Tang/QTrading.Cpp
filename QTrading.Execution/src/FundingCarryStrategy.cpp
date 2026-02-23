#include "Execution/FundingCarryStrategy.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Execution {

FundingCarryStrategy::FundingCarryStrategy(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    QTrading::Universe::FixedUniverseSelector& universe_selector,
    QTrading::Signal::FundingCarrySignalEngine& signal_engine,
    QTrading::Intent::FundingCarryIntentBuilder& intent_builder,
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
    , execution_orchestrator_(
        execution_engine_,
        execution_scheduler_,
        execution_policy_)
    , exchange_gateway_(exchange_, std::move(instrument_types))
{
}

void FundingCarryStrategy::wait_for_done()
{
    auto market_opt = exchange_->get_market_channel()->Receive();
    if (!market_opt.has_value()) {
        return;
    }
    const auto& market = market_opt.value();

    (void)universe_selector_.select();
    const auto signal = signal_engine_.on_market(market);
    const auto intent = intent_builder_.build(signal, market);
    const auto account = exchange_gateway_.BuildAccountState();
    const auto risk = risk_engine_.position(intent, account, market);
    const auto orders = execution_orchestrator_.Execute(risk, account, signal, market);
    exchange_gateway_.SubmitOrders(orders);
    exchange_gateway_.ApplyMonitoringAlerts(monitoring_.check(account));
}

} // namespace QTrading::Execution
