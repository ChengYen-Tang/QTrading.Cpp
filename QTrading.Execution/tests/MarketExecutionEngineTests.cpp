#include "Execution/MarketExecutionEngine.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

class FakeExchange final : public QTrading::Infra::Exchanges::IExchange<MarketPtr> {
public:
    bool place_order(const std::string&, double, double,
        QTrading::Dto::Trading::OrderSide,
        QTrading::Dto::Trading::PositionSide,
        bool) override { return true; }
    bool place_order(const std::string&, double,
        QTrading::Dto::Trading::OrderSide,
        QTrading::Dto::Trading::PositionSide,
        bool) override { return true; }
    void close_position(const std::string&, double) override {}
    void close_position(const std::string&) override {}
    void close_position(const std::string&,
        QTrading::Dto::Trading::PositionSide,
        double) override {}
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
