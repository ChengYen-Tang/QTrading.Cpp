#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/ExecutionOrchestrator.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Execution/LiquidityAwareExecutionScheduler.hpp"
#include "Intent/BasisArbitrageIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/BasisArbitrageSignalEngine.hpp"
#include "Strategy/FundingCarryStrategyGateway.hpp"
#include "Strategy/IStrategyRuntime.hpp"
#include "Universe/IUniverseSelector.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Strategy {

class BasisArbitrageMultiPairRuntime : public IStrategyRuntime {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    BasisArbitrageMultiPairRuntime(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        QTrading::Universe::IUniverseSelector& universe_selector,
        QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
        QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg,
        QTrading::Risk::SimpleRiskEngine& risk_engine,
        QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
        QTrading::Monitoring::SimpleMonitoring& monitoring,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    void RunOneCycle() override;

private:
    struct PairContext {
        std::string raw_symbol;
        std::string spot_symbol;
        std::string perp_symbol;
        QTrading::Signal::BasisArbitrageSignalEngine signal_engine;
        QTrading::Intent::BasisArbitrageIntentBuilder intent_builder;

        PairContext(
            std::string raw_symbol,
            std::string spot_symbol,
            std::string perp_symbol,
            QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
            QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg);
    };

    void InitializePairsIfNeeded(const MarketPtr& market);
    struct PairSignalSnapshot {
        std::size_t pair_index = 0;
        QTrading::Signal::SignalDecision signal;
    };
    std::vector<PairSignalSnapshot> BuildActivePairRanking(const MarketPtr& market);
    std::unordered_set<std::size_t> CollectExposedPairIndexes(const QTrading::Risk::AccountState& account) const;
    bool AccountHasExposure(const QTrading::Risk::AccountState& account) const;
    QTrading::Risk::RiskTarget ScaleRiskTarget(const QTrading::Risk::RiskTarget& input, double scale) const;
    void MergeRiskTarget(const QTrading::Risk::RiskTarget& input, QTrading::Risk::RiskTarget& merged) const;

    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    QTrading::Universe::IUniverseSelector& universe_selector_;
    QTrading::Signal::BasisArbitrageSignalEngine::Config base_signal_cfg_;
    QTrading::Intent::BasisArbitrageIntentBuilder::Config base_intent_cfg_;
    QTrading::Risk::SimpleRiskEngine& risk_engine_;
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine_;
    QTrading::Monitoring::SimpleMonitoring& monitoring_;
    QTrading::Execution::LiquidityAwareExecutionScheduler execution_scheduler_;
    QTrading::Execution::TargetNotionalExecutionPolicy execution_policy_;
    QTrading::Execution::ExecutionOrchestrator execution_orchestrator_;
    FundingCarryStrategyGateway exchange_gateway_;
    std::vector<PairContext> pair_contexts_;
};

} // namespace QTrading::Strategy
