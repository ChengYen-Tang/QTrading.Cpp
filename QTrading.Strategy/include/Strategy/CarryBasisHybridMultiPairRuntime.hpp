#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/ExecutionOrchestrator.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Execution/LiquidityAwareExecutionScheduler.hpp"
#include "Intent/CarryBasisHybridIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/CarryBasisHybridSignalEngine.hpp"
#include "Strategy/FundingCarryStrategyGateway.hpp"
#include "Strategy/IStrategyRuntime.hpp"
#include "Strategy/StrategyConfigLoader.hpp"
#include "Universe/IUniverseSelector.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Strategy {

class CarryBasisHybridMultiPairRuntime final : public IStrategyRuntime {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    CarryBasisHybridMultiPairRuntime(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        QTrading::Universe::IUniverseSelector& universe_selector,
        QTrading::Signal::CarryBasisHybridSignalEngine::Config signal_cfg,
        QTrading::Intent::CarryBasisHybridIntentBuilder::Config intent_cfg,
        StrategyRuntimeConfig runtime_cfg,
        QTrading::Risk::SimpleRiskEngine& risk_engine,
        QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
        QTrading::Monitoring::SimpleMonitoring& monitoring,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    void RunOneCycle() override;

private:
    struct PairStaticInfo {
        std::string raw_symbol;
        std::string spot_symbol;
        std::string perp_symbol;
        std::size_t spot_market_id = 0;
        std::size_t perp_market_id = 0;
    };

    struct PairRuntimeState {
        QTrading::Signal::CarryBasisHybridSignalEngine signal_engine;
        QTrading::Intent::CarryBasisHybridIntentBuilder intent_builder;

        PairRuntimeState(
            std::string spot_symbol,
            std::string perp_symbol,
            QTrading::Signal::CarryBasisHybridSignalEngine::Config signal_cfg,
            QTrading::Intent::CarryBasisHybridIntentBuilder::Config intent_cfg);
    };

    struct PairSignalSnapshot {
        std::size_t pair_index = 0;
        QTrading::Signal::SignalDecision signal;
        double portfolio_weight = 0.0;
    };

    void InitializePairsIfNeeded(const MarketPtr& market);
    std::vector<PairSignalSnapshot> BuildActivePairRanking(const MarketPtr& market);
    std::unordered_set<std::size_t> CollectExposedPairIndexes(const QTrading::Risk::AccountState& account) const;
    QTrading::Risk::RiskTarget ScaleRiskTarget(const QTrading::Risk::RiskTarget& input, double scale) const;
    void MergeRiskTarget(const QTrading::Risk::RiskTarget& input, QTrading::Risk::RiskTarget& merged) const;
    bool PassesTurnoverEconomicsGate(
        const PairStaticInfo& pair,
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    QTrading::Risk::RiskTarget FreezePairToCurrentRiskTarget(
        const PairStaticInfo& pair,
        const QTrading::Risk::RiskTarget& reference,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    void ClampPairTargetDelta(
        const PairStaticInfo& pair,
        QTrading::Risk::RiskTarget& target,
        const MarketPtr& market) const;
    void CapPairTargetNotional(
        const PairStaticInfo& pair,
        QTrading::Risk::RiskTarget& target) const;
    void ClampPairTargetLiquidity(
        const PairStaticInfo& pair,
        QTrading::Risk::RiskTarget& target,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    void CancelOpenOrdersForPair(const PairStaticInfo& pair) const;
    double QuoteVolume(const PairStaticInfo& pair, bool spot_leg, const MarketPtr& market) const;
    double CurrentSignedNotional(
        const std::string& symbol,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    double CurrentSignedOpenOrderNotional(
        const std::string& symbol,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    double LastPrice(const std::string& symbol, const MarketPtr& market) const;
    double LatestFundingRate(const std::string& symbol, const MarketPtr& market) const;

    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    QTrading::Universe::IUniverseSelector& universe_selector_;
    QTrading::Signal::CarryBasisHybridSignalEngine::Config base_signal_cfg_;
    QTrading::Intent::CarryBasisHybridIntentBuilder::Config base_intent_cfg_;
    StrategyRuntimeConfig runtime_cfg_;
    QTrading::Risk::SimpleRiskEngine& risk_engine_;
    QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine_;
    QTrading::Monitoring::SimpleMonitoring& monitoring_;
    QTrading::Execution::LiquidityAwareExecutionScheduler execution_scheduler_;
    QTrading::Execution::TargetNotionalExecutionPolicy execution_policy_;
    QTrading::Execution::ExecutionOrchestrator execution_orchestrator_;
    FundingCarryStrategyGateway exchange_gateway_;
    std::vector<PairStaticInfo> pair_static_infos_;
    std::vector<PairRuntimeState> pair_runtime_states_;
    std::unordered_map<std::string, std::size_t> symbol_to_pair_index_;
    std::vector<PairSignalSnapshot> ranking_buffer_;
};

} // namespace QTrading::Strategy
