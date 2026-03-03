#include "Execution/MarketExecutionEngine.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

class FakeExchange final : public QTrading::Infra::Exchanges::IExchange<MarketPtr> {
public:
    bool step() override { return false; }
    const std::vector<QTrading::dto::Position>& get_all_positions() const override { return positions_; }
    const std::vector<QTrading::dto::Order>& get_all_open_orders() const override { return orders_; }
    void set_symbol_leverage(const std::string& symbol, double new_leverage) override
    {
        leverage_[symbol] = new_leverage;
    }
    double get_symbol_leverage(const std::string& symbol) const override
    {
        auto it = leverage_.find(symbol);
        return (it == leverage_.end()) ? 1.0 : it->second;
    }

    std::vector<QTrading::dto::Position> positions_;
    std::vector<QTrading::dto::Order> orders_;
    std::unordered_map<std::string, double> leverage_;
};

MarketPtr MakeMarket(unsigned long long ts, const std::string& symbol, double close)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto k(ts, 0, 0, 0, close, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol);
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = k;
    return dto;
}

MarketPtr MakeMarketWithQuoteVolume(
    unsigned long long ts,
    const std::string& symbol,
    double close,
    double quote_volume)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto k(ts, 0, 0, 0, close, 0, ts, quote_volume, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol);
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = k;
    return dto;
}

MarketPtr MakeTwoSymbolMarket(
    unsigned long long ts,
    const std::string& symbol_a,
    double close_a,
    const std::string& symbol_b,
    double close_b)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol_a);
    symbols->push_back(symbol_b);
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, close_a, 0, ts, 0, 0, 0, 0);
    dto->klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, close_b, 0, ts, 0, 0, 0, 0);
    return dto;
}

} // namespace

TEST(MarketExecutionEngineTests, GeneratesOrderForNotionalDelta)
{
    auto ex = std::make_shared<FakeExchange>();
    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    pos.quantity = 1.0;
    pos.is_long = true;
    ex->positions_.push_back(pos);

    QTrading::Execution::MarketExecutionEngine engine(ex, { 10.0 });
    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 3.0;

    QTrading::Signal::SignalDecision signal;
    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].action, QTrading::Execution::OrderAction::Sell);
    EXPECT_NEAR(orders[0].qty, 0.9, 1e-6);
    EXPECT_TRUE(orders[0].reduce_only);
    EXPECT_DOUBLE_EQ(ex->get_symbol_leverage("BTCUSDT_PERP"), 3.0);
}

TEST(MarketExecutionEngineTests, PlansMixedTypedSymbolsWithoutSuffixAssumptions)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine engine(ex, { 10.0 });
    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_CASH"] = 500.0;
    target.target_positions["BTCUSDT_SWAP"] = -1200.0;
    target.leverage["BTCUSDT_CASH"] = 1.0;
    target.leverage["BTCUSDT_SWAP"] = 3.0;

    QTrading::Signal::SignalDecision signal;
    auto market = MakeTwoSymbolMarket(1, "BTCUSDT_CASH", 100.0, "BTCUSDT_SWAP", 3000.0);
    auto orders = engine.plan(target, signal, market);

    ASSERT_EQ(orders.size(), 2u);
    const auto find_order = [&](const std::string& symbol) -> const QTrading::Execution::ExecutionOrder* {
        for (const auto& order : orders) {
            if (order.symbol == symbol) {
                return &order;
            }
        }
        return nullptr;
    };

    const auto* cash = find_order("BTCUSDT_CASH");
    ASSERT_NE(cash, nullptr);
    EXPECT_EQ(cash->action, QTrading::Execution::OrderAction::Buy);
    EXPECT_NEAR(cash->qty, 5.0, 1e-9);

    const auto* swap = find_order("BTCUSDT_SWAP");
    ASSERT_NE(swap, nullptr);
    EXPECT_EQ(swap->action, QTrading::Execution::OrderAction::Sell);
    EXPECT_NEAR(swap->qty, 0.4, 1e-9);

    EXPECT_DOUBLE_EQ(ex->get_symbol_leverage("BTCUSDT_CASH"), 1.0);
    EXPECT_DOUBLE_EQ(ex->get_symbol_leverage("BTCUSDT_SWAP"), 3.0);
}

TEST(MarketExecutionEngineTests, CarryRebalanceRespectsCooldown)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 1000;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto first = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(first.size(), 1u);

    auto second = engine.plan(target, signal, MakeMarket(500, "BTCUSDT_PERP", 10000.0));
    EXPECT_TRUE(second.empty());

    auto third = engine.plan(target, signal, MakeMarket(1500, "BTCUSDT_PERP", 10000.0));
    EXPECT_EQ(third.size(), 1u);
}

TEST(MarketExecutionEngineTests, CarryLargeNotionalUsesLongerCooldown)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 1000;
    cfg.carry_large_notional_cooldown_ms = 3000;
    cfg.carry_large_notional_threshold = 500.0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_large_notional_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0; // Large notional branch.
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto first = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(first.size(), 1u);

    // Base cooldown has passed, but large-notional cooldown has not.
    auto second = engine.plan(target, signal, MakeMarket(2000, "BTCUSDT_PERP", 10000.0));
    EXPECT_TRUE(second.empty());

    auto third = engine.plan(target, signal, MakeMarket(3500, "BTCUSDT_PERP", 10000.0));
    EXPECT_EQ(third.size(), 1u);
}

TEST(MarketExecutionEngineTests, CarryRebalanceStepRatioCapsOrderSize)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 0.1;
    cfg.carry_bootstrap_step_ratio = 0.1; // Match base step to isolate step-ratio cap behavior.
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(orders.size(), 1u);
    // Target notional is 1000 and step ratio is 10%, so one rebalance should cap at 100 notional.
    EXPECT_NEAR(orders[0].qty, 0.01, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryRebalanceRespectsParticipationCap)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 0.05; // 5% of current-bar quote volume.
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(
        target,
        signal,
        MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1000.0));
    ASSERT_EQ(orders.size(), 1u);
    // Volume cap = quote_volume * rate = 1000 * 0.05 = 50 notional.
    EXPECT_NEAR(orders[0].qty, 50.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryRebalanceRespectsDailyQuota)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_max_rebalances_per_day = 1;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto first = engine.plan(target, signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    ASSERT_EQ(first.size(), 1u);

    auto second = engine.plan(target, signal, MakeMarketWithQuoteVolume(2, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    EXPECT_TRUE(second.empty());

    auto third = engine.plan(
        target,
        signal,
        MakeMarketWithQuoteVolume(86'400'000ull + 1ull, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    EXPECT_EQ(third.size(), 1u);
}

TEST(MarketExecutionEngineTests, CarryWindowBudgetCapsAndResetsByWindow)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 1.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    cfg.carry_window_budget_enabled = true;
    cfg.carry_window_budget_ms = 1000;
    cfg.carry_window_budget_participation_rate = 0.05;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto first = engine.plan(target, signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1000.0));
    ASSERT_EQ(first.size(), 1u);
    // Window budget = 1000 * 0.05 = 50 notional.
    EXPECT_NEAR(first[0].qty, 50.0 / 10000.0, 1e-9);

    auto second = engine.plan(target, signal, MakeMarketWithQuoteVolume(200, "BTCUSDT_PERP", 10000.0, 0.0));
    // Same window, no new quote volume, remaining budget is zero.
    EXPECT_TRUE(second.empty());

    auto third = engine.plan(target, signal, MakeMarketWithQuoteVolume(1500, "BTCUSDT_PERP", 10000.0, 1000.0));
    ASSERT_EQ(third.size(), 1u);
    // Budget resets in new window.
    EXPECT_NEAR(third[0].qty, 50.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryLargeNotionalUsesMoreConservativeStepRatio)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 0.20;
    cfg.carry_large_notional_step_ratio = 0.05;
    cfg.carry_large_notional_threshold = 500.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    ASSERT_EQ(orders.size(), 1u);
    // Step ratio should be capped to 5% because target notional exceeds large-notional threshold.
    EXPECT_NEAR(orders[0].qty, 50.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryMinNotionalRatioPreventsTinyLargeBookRebalances)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    pos.quantity = 0.095; // ~950 notional at price 10000
    pos.is_long = true;
    ex->positions_.push_back(pos);

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_large_notional_step_ratio = 1.0;
    cfg.carry_large_notional_threshold = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.10; // 10% of target notional as dynamic min.
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    // Delta is only 50 notional; dynamic min notional is 100, so this micro rebalance is skipped.
    EXPECT_TRUE(orders.empty());
}

TEST(MarketExecutionEngineTests, CarryBootstrapModeAcceleratesInitialConvergence)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 60'000;
    cfg.carry_max_rebalance_step_ratio = 0.06;
    cfg.carry_max_participation_rate = 0.001;
    cfg.carry_bootstrap_gap_ratio = 0.25;
    cfg.carry_bootstrap_step_ratio = 1.0;
    cfg.carry_bootstrap_participation_rate = 0.05;
    cfg.carry_bootstrap_cooldown_ms = 0;
    cfg.carry_large_notional_threshold = 1'000'000.0; // Avoid large-notional clamp in this test.
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(
        target,
        signal,
        MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1000.0));

    ASSERT_EQ(orders.size(), 1u);
    // Bootstrap upgrades both step and participation caps, so first order can be 50 notional.
    EXPECT_NEAR(orders[0].qty, 50.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryConfidenceAdaptiveStepScaleDependsOnSignalConfidence)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 0.10;
    cfg.carry_bootstrap_step_ratio = 0.0; // Prevent bootstrap override from masking confidence scaling.
    cfg.carry_bootstrap_participation_rate = 0.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    cfg.carry_confidence_adaptive_enabled = true;
    cfg.carry_confidence_step_scale_min = 0.5;
    cfg.carry_confidence_step_scale_max = 1.5;
    cfg.carry_confidence_participation_scale_min = 1.0;
    cfg.carry_confidence_participation_scale_max = 1.0;
    cfg.carry_confidence_cooldown_scale_min = 1.0;
    cfg.carry_confidence_cooldown_scale_max = 1.0;

    QTrading::Execution::MarketExecutionEngine low_engine(ex, cfg);
    QTrading::Execution::MarketExecutionEngine high_engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision low_signal;
    low_signal.strategy = "funding_carry";
    low_signal.urgency = QTrading::Signal::SignalUrgency::Low;
    low_signal.confidence = 0.0;

    QTrading::Signal::SignalDecision high_signal = low_signal;
    high_signal.confidence = 1.0;

    auto low_orders = low_engine.plan(target, low_signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1'000'000.0));
    auto high_orders = high_engine.plan(target, high_signal, MakeMarketWithQuoteVolume(1, "BTCUSDT_PERP", 10000.0, 1'000'000.0));

    ASSERT_EQ(low_orders.size(), 1u);
    ASSERT_EQ(high_orders.size(), 1u);
    // Low confidence: step ratio = 0.10 * 0.5 = 0.05 -> 50 notional -> 0.005 BTC.
    EXPECT_NEAR(low_orders[0].qty, 50.0 / 10000.0, 1e-9);
    // High confidence: step ratio = 0.10 * 1.5 = 0.15 -> 150 notional -> 0.015 BTC.
    EXPECT_NEAR(high_orders[0].qty, 150.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryConfidenceAdaptiveCooldownScalesByConfidence)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 1000;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_large_notional_threshold = 1'000'000.0; // Keep large-notional branch disabled.
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    cfg.carry_confidence_adaptive_enabled = true;
    cfg.carry_confidence_step_scale_min = 1.0;
    cfg.carry_confidence_step_scale_max = 1.0;
    cfg.carry_confidence_participation_scale_min = 1.0;
    cfg.carry_confidence_participation_scale_max = 1.0;
    cfg.carry_confidence_cooldown_scale_min = 0.5;
    cfg.carry_confidence_cooldown_scale_max = 2.0;

    QTrading::Execution::MarketExecutionEngine low_engine(ex, cfg);
    QTrading::Execution::MarketExecutionEngine high_engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision low_signal;
    low_signal.strategy = "funding_carry";
    low_signal.urgency = QTrading::Signal::SignalUrgency::Low;
    low_signal.confidence = 0.0;

    QTrading::Signal::SignalDecision high_signal = low_signal;
    high_signal.confidence = 1.0;

    ASSERT_EQ(low_engine.plan(target, low_signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0)).size(), 1u);
    // Low confidence cooldown = 1000 * 2.0 = 2000ms, so 1200ms later should still be blocked.
    EXPECT_TRUE(low_engine.plan(target, low_signal, MakeMarket(1200, "BTCUSDT_PERP", 10000.0)).empty());

    ASSERT_EQ(high_engine.plan(target, high_signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0)).size(), 1u);
    // High confidence cooldown = 1000 * 0.5 = 500ms, so 600ms later should pass.
    EXPECT_EQ(high_engine.plan(target, high_signal, MakeMarket(600, "BTCUSDT_PERP", 10000.0)).size(), 1u);
}

TEST(MarketExecutionEngineTests, CarryRequireTwoSidedRebalanceSkipsOneSidedOrder)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_require_two_sided_rebalance = true;

    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    EXPECT_TRUE(orders.empty());
}

TEST(MarketExecutionEngineTests, CarryBalanceTwoSidedRebalanceClipsLargerSide)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 10.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_require_two_sided_rebalance = true;
    cfg.carry_balance_two_sided_rebalance = true;

    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_SPOT"] = 2000.0;
    target.target_positions["BTCUSDT_PERP"] = -1000.0;
    target.leverage["BTCUSDT_SPOT"] = 1.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto market = MakeTwoSymbolMarket(1, "BTCUSDT_SPOT", 10000.0, "BTCUSDT_PERP", 10000.0);
    auto orders = engine.plan(target, signal, market);

    ASSERT_EQ(orders.size(), 2u);
    const auto find_order = [&](const std::string& symbol) -> const QTrading::Execution::ExecutionOrder* {
        for (const auto& order : orders) {
            if (order.symbol == symbol) {
                return &order;
            }
        }
        return nullptr;
    };

    const auto* spot = find_order("BTCUSDT_SPOT");
    const auto* perp = find_order("BTCUSDT_PERP");
    ASSERT_NE(spot, nullptr);
    ASSERT_NE(perp, nullptr);
    EXPECT_EQ(spot->action, QTrading::Execution::OrderAction::Buy);
    EXPECT_EQ(perp->action, QTrading::Execution::OrderAction::Sell);
    // Spot leg is clipped from 2000 notional to 1000 notional to match perp side.
    EXPECT_NEAR(spot->qty, 1000.0 / 10000.0, 1e-9);
    EXPECT_NEAR(perp->qty, 1000.0 / 10000.0, 1e-9);
}

TEST(MarketExecutionEngineTests, CarryMakerFirstUsesLimitOrderWhenGapIsSmall)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    pos.quantity = 0.097; // ~970 notional at price 10000
    pos.is_long = true;
    ex->positions_.push_back(pos);

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 5.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_maker_first_enabled = true;
    cfg.carry_maker_limit_offset_bps = 1.0;
    cfg.carry_maker_catchup_gap_ratio = 0.10;

    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0; // delta ~30 notional => small gap
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, QTrading::Execution::OrderType::Limit);
    EXPECT_LT(orders[0].price, 10000.0); // buy maker below reference
}

TEST(MarketExecutionEngineTests, CarryMakerFirstFallsBackToMarketWhenGapIsLarge)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 5.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_maker_first_enabled = true;
    cfg.carry_maker_limit_offset_bps = 1.0;
    cfg.carry_maker_catchup_gap_ratio = 0.10;

    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0; // delta ~1000 notional => large gap
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, QTrading::Execution::OrderType::Market);
    EXPECT_DOUBLE_EQ(orders[0].price, 0.0);
}

TEST(MarketExecutionEngineTests, CarryTargetAnchorSuppressesSmallTargetJitter)
{
    auto ex = std::make_shared<FakeExchange>();

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    pos.quantity = 0.1; // ~1000 notional at price 10000
    pos.is_long = true;
    ex->positions_.push_back(pos);

    QTrading::Execution::MarketExecutionEngine::Config cfg;
    cfg.min_notional = 1.0;
    cfg.carry_rebalance_cooldown_ms = 0;
    cfg.carry_max_rebalance_step_ratio = 1.0;
    cfg.carry_max_participation_rate = 1.0;
    cfg.carry_min_rebalance_notional_ratio = 0.0;
    cfg.carry_target_anchor_enabled = true;
    cfg.carry_target_anchor_update_ratio = 0.02; // Need >=2% target drift to move anchor.

    QTrading::Execution::MarketExecutionEngine engine(ex, cfg);

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    QTrading::Risk::RiskTarget target_small_drift;
    target_small_drift.target_positions["BTCUSDT_PERP"] = 1010.0; // +1% from anchored 1000.
    target_small_drift.leverage["BTCUSDT_PERP"] = 2.0;
    auto small_orders = engine.plan(target_small_drift, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));
    EXPECT_TRUE(small_orders.empty());

    QTrading::Risk::RiskTarget target_large_drift;
    target_large_drift.target_positions["BTCUSDT_PERP"] = 1050.0; // +5% from anchored 1000.
    target_large_drift.leverage["BTCUSDT_PERP"] = 2.0;
    auto large_orders = engine.plan(target_large_drift, signal, MakeMarket(2, "BTCUSDT_PERP", 10000.0));
    ASSERT_EQ(large_orders.size(), 1u);
    EXPECT_EQ(large_orders[0].action, QTrading::Execution::OrderAction::Buy);
    // 50 notional delta => 0.005 BTC at price 10000.
    EXPECT_NEAR(large_orders[0].qty, 50.0 / 10000.0, 1e-9);
}
