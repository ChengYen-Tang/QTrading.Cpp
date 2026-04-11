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
#include "Strategy/StrategyConfigLoader.hpp"
#include "Strategy/IStrategyRuntime.hpp"
#include "Universe/IUniverseSelector.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <condition_variable>
#include <exception>

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
        StrategyRuntimeConfig runtime_cfg,
        QTrading::Risk::SimpleRiskEngine& risk_engine,
        QTrading::Execution::IExecutionEngine<MarketPtr>& execution_engine,
        QTrading::Monitoring::SimpleMonitoring& monitoring,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);
    ~BasisArbitrageMultiPairRuntime() override;

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
        struct QualityState {
            std::vector<double> abs_basis_window;
            std::vector<double> abs_basis_scratch;
            std::vector<double> quote_ratio_window;
            std::vector<unsigned char> spot_zero_window;
            std::size_t next_index = 0;
            std::size_t sample_count = 0;
            std::size_t spot_zero_count = 0;
            double quote_ratio_sum = 0.0;
            std::size_t structural_sample_count = 0;
            std::size_t structural_spot_zero_count = 0;
            double structural_abs_basis_sum = 0.0;
            double structural_quote_ratio_sum = 0.0;
        };

        QTrading::Signal::BasisArbitrageSignalEngine signal_engine;
        QTrading::Intent::BasisArbitrageIntentBuilder intent_builder;
        QualityState quality_state;

        PairRuntimeState(
            std::string spot_symbol,
            std::string perp_symbol,
            QTrading::Signal::BasisArbitrageSignalEngine::Config signal_cfg,
            QTrading::Intent::BasisArbitrageIntentBuilder::Config intent_cfg);
    };

    void InitializePairsIfNeeded(const MarketPtr& market);
    struct PairSignalSnapshot {
        std::size_t pair_index = 0;
        QTrading::Signal::SignalDecision signal;
        double portfolio_weight = 0.0;
    };
    struct PairShard {
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    struct WorkerState {
        std::vector<std::size_t> shard_indexes;
        std::vector<PairSignalSnapshot> output;
        std::thread thread;
    };

    std::vector<PairSignalSnapshot> BuildActivePairRanking(const MarketPtr& market);
    void ApplyPortfolioAllocator(std::vector<PairSignalSnapshot>& ranked_pairs) const;
    void RebuildShards();
    void InitializeWorkersIfNeeded();
    void StopWorkers();
    void WorkerLoop(std::size_t worker_index);
    void CollectActivePairsInShard(
        const MarketPtr& market,
        const PairShard& shard,
        std::vector<PairSignalSnapshot>& out);
    void UpdatePairQualityState(std::size_t pair_index, const MarketPtr& market);
    bool PairPassesQualityGate(std::size_t pair_index);
    bool PairHasTradableLiquidityThisCycle(std::size_t pair_index, const MarketPtr& market) const;
    std::unordered_set<std::size_t> CollectExposedPairIndexes(const QTrading::Risk::AccountState& account) const;
    QTrading::Risk::RiskTarget ScaleRiskTarget(const QTrading::Risk::RiskTarget& input, double scale) const;
    void MergeRiskTarget(const QTrading::Risk::RiskTarget& input, QTrading::Risk::RiskTarget& merged) const;
    void NormalizePairTargetsToAvoidSingleLegExposure(
        QTrading::Risk::RiskTarget& target,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;
    std::vector<QTrading::Execution::ExecutionOrder> FilterOrdersToAvoidSingleLegExposure(
        const std::vector<QTrading::Execution::ExecutionOrder>& orders,
        const QTrading::Risk::AccountState& account,
        const MarketPtr& market) const;

    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    QTrading::Universe::IUniverseSelector& universe_selector_;
    QTrading::Signal::BasisArbitrageSignalEngine::Config base_signal_cfg_;
    QTrading::Intent::BasisArbitrageIntentBuilder::Config base_intent_cfg_;
    StrategyRuntimeConfig runtime_cfg_;
    std::unordered_set<std::string> allowed_raw_symbols_;
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
    std::vector<PairShard> shards_;
    std::vector<PairSignalSnapshot> ranking_buffer_;
    std::vector<int> active_pair_slots_;
    std::vector<std::size_t> active_pair_slot_touched_;
    std::vector<WorkerState> workers_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::condition_variable worker_done_cv_;
    MarketPtr current_market_;
    std::size_t current_generation_ = 0;
    std::size_t completed_workers_ = 0;
    bool worker_stop_requested_ = false;
    bool workers_initialized_ = false;
    std::exception_ptr worker_exception_;
};

} // namespace QTrading::Strategy
