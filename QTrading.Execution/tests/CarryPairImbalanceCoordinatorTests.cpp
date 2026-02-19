#include "Execution/CarryPairImbalanceCoordinator.hpp"

#include <gtest/gtest.h>

#include <memory>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

MarketPtr MakeTwoSymbolMarket(
    uint64_t ts,
    const std::string& symbol_a,
    double close_a,
    double qv_a,
    const std::string& symbol_b,
    double close_b,
    double qv_b)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol_a);
    symbols->push_back(symbol_b);
    dto->symbols = symbols;
    dto->klines_by_id.resize(2);
    dto->klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(
        ts, 0, 0, 0, close_a, 0, ts, qv_a, 0, 0, 0);
    dto->klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(
        ts, 0, 0, 0, close_b, 0, ts, qv_b, 0, 0, 0);
    return dto;
}

} // namespace

TEST(CarryPairImbalanceCoordinatorTests, ClipsLargerSideWhenBalanceEnabled)
{
    QTrading::Execution::CarryPairImbalanceCoordinator::Config cfg;
    cfg.enabled = true;
    cfg.apply_only_funding_carry = true;
    cfg.apply_only_low_urgency = true;
    cfg.ignore_reduce_only_orders = true;
    cfg.require_two_sided = false;
    cfg.balance_two_sided = true;
    cfg.min_notional_usdt = 5.0;

    QTrading::Execution::CarryPairImbalanceCoordinator coordinator(cfg);

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    std::vector<QTrading::Execution::ExecutionOrder> orders;
    QTrading::Execution::ExecutionOrder buy{};
    buy.symbol = "BTCUSDT_SPOT";
    buy.action = QTrading::Execution::OrderAction::Buy;
    buy.qty = 0.20; // 2000 notional at 10000
    orders.push_back(buy);

    QTrading::Execution::ExecutionOrder sell{};
    sell.symbol = "BTCUSDT_PERP";
    sell.action = QTrading::Execution::OrderAction::Sell;
    sell.qty = 0.10; // 1000 notional at 10000
    orders.push_back(sell);

    const auto market = MakeTwoSymbolMarket(
        1,
        "BTCUSDT_SPOT",
        10'000.0,
        1'000'000.0,
        "BTCUSDT_PERP",
        10'000.0,
        1'000'000.0);

    const auto adjusted = coordinator.Coordinate(orders, signal, market);
    ASSERT_EQ(adjusted.size(), 2u);
    EXPECT_NEAR(adjusted[0].qty, 0.10, 1e-12);
    EXPECT_NEAR(adjusted[1].qty, 0.10, 1e-12);
}

TEST(CarryPairImbalanceCoordinatorTests, DropsCarryOrdersWhenRequireTwoSidedAndOnlyOneSideExists)
{
    QTrading::Execution::CarryPairImbalanceCoordinator::Config cfg;
    cfg.enabled = true;
    cfg.apply_only_funding_carry = true;
    cfg.apply_only_low_urgency = true;
    cfg.require_two_sided = true;
    cfg.balance_two_sided = true;

    QTrading::Execution::CarryPairImbalanceCoordinator coordinator(cfg);

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Signal::SignalUrgency::Low;

    std::vector<QTrading::Execution::ExecutionOrder> orders;
    QTrading::Execution::ExecutionOrder buy{};
    buy.symbol = "BTCUSDT_SPOT";
    buy.action = QTrading::Execution::OrderAction::Buy;
    buy.qty = 0.10;
    orders.push_back(buy);

    const auto market = MakeTwoSymbolMarket(
        1,
        "BTCUSDT_SPOT",
        10'000.0,
        1'000'000.0,
        "BTCUSDT_PERP",
        10'000.0,
        1'000'000.0);

    const auto adjusted = coordinator.Coordinate(orders, signal, market);
    EXPECT_TRUE(adjusted.empty());
}

TEST(CarryPairImbalanceCoordinatorTests, ConfidenceAdaptiveMaxLegNotionalScalesOrderCap)
{
    QTrading::Execution::CarryPairImbalanceCoordinator::Config cfg;
    cfg.enabled = true;
    cfg.apply_only_funding_carry = true;
    cfg.apply_only_low_urgency = true;
    cfg.require_two_sided = false;
    cfg.balance_two_sided = false;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.carry_confidence_adaptive_enabled = true;
    cfg.carry_confidence_max_leg_scale_min = 0.5;
    cfg.carry_confidence_max_leg_scale_max = 1.5;

    QTrading::Execution::CarryPairImbalanceCoordinator low_coordinator(cfg);
    QTrading::Execution::CarryPairImbalanceCoordinator high_coordinator(cfg);

    QTrading::Signal::SignalDecision low_signal;
    low_signal.strategy = "funding_carry";
    low_signal.urgency = QTrading::Signal::SignalUrgency::Low;
    low_signal.confidence = 0.0;

    QTrading::Signal::SignalDecision high_signal = low_signal;
    high_signal.confidence = 1.0;

    std::vector<QTrading::Execution::ExecutionOrder> orders;
    QTrading::Execution::ExecutionOrder buy{};
    buy.symbol = "BTCUSDT_SPOT";
    buy.action = QTrading::Execution::OrderAction::Buy;
    buy.qty = 0.20; // 2000 notional at 10000
    orders.push_back(buy);

    const auto market = MakeTwoSymbolMarket(
        1,
        "BTCUSDT_SPOT",
        10'000.0,
        1'000'000.0,
        "BTCUSDT_PERP",
        10'000.0,
        1'000'000.0);

    const auto low_adjusted = low_coordinator.Coordinate(orders, low_signal, market);
    const auto high_adjusted = high_coordinator.Coordinate(orders, high_signal, market);

    ASSERT_EQ(low_adjusted.size(), 1u);
    ASSERT_EQ(high_adjusted.size(), 1u);
    // low cap: 1000*0.5 = 500 notional -> 0.05 qty.
    EXPECT_NEAR(low_adjusted[0].qty, 0.05, 1e-12);
    // high cap: 1000*1.5 = 1500 notional -> 0.15 qty.
    EXPECT_NEAR(high_adjusted[0].qty, 0.15, 1e-12);
}
