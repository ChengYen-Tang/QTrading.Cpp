#include <gtest/gtest.h>

#include <unordered_map>
#include <vector>

#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"
#include "Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionService.hpp"

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;
using QTrading::Infra::Exchanges::BinanceSim::AccountPolicies;
using QTrading::Infra::Exchanges::BinanceSim::Domain::AccountPolicyExecutionService;

namespace {

std::unordered_map<std::string, TradeKlineDto> oneKline(
    const std::string& symbol,
    double price,
    double volume)
{
    TradeKlineDto kline{};
    kline.OpenPrice = price;
    kline.HighPrice = price;
    kline.LowPrice = price;
    kline.ClosePrice = price;
    kline.Volume = volume;
    return { { symbol, kline } };
}

double sum_position_qty(const std::vector<QTrading::dto::Position>& positions)
{
    double total = 0.0;
    for (const auto& position : positions) {
        total += position.quantity;
    }
    return total;
}

} // namespace

TEST(AccountPolicyExecutionServiceTest, PlaceOrderSuccessCheckOpenOrders)
{
    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};

    const bool ok = AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        2.0,
        100.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders);

    ASSERT_TRUE(ok);
    ASSERT_EQ(open_orders.size(), 1u);
    EXPECT_DOUBLE_EQ(open_orders[0].quantity, 2.0);
    EXPECT_DOUBLE_EQ(open_orders[0].price, 100.0);
}

TEST(AccountPolicyExecutionServiceTest, PerpTradeConsumesOnlyPerpWallet)
{
    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};
    std::unordered_map<std::string, double> symbol_leverage{ { "BTCUSDT", 10.0 } };

    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        1.0,
        100.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    const AccountPolicies policies = AccountPolicies::Default();
    const auto result = AccountPolicyExecutionService::ApplyUpdates(
        oneKline("BTCUSDT", 100.0, 1000.0),
        {},
        policies,
        0,
        symbol_leverage,
        open_orders,
        positions);

    EXPECT_LT(result.perp_wallet_delta, 0.0);
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].symbol, "BTCUSDT");
}

TEST(AccountPolicyExecutionServiceTest, UpdatePositionsPartialFillSameOrder)
{
    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};

    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        2.0,
        100.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    const AccountPolicies policies = AccountPolicies::Default();
    const auto first = AccountPolicyExecutionService::ApplyUpdates(
        oneKline("BTCUSDT", 100.0, 1.0),
        {},
        policies,
        0,
        {},
        open_orders,
        positions);
    EXPECT_EQ(first.filled_count, 1u);
    ASSERT_EQ(open_orders.size(), 1u);
    EXPECT_NEAR(open_orders[0].quantity, 1.0, 1e-12);
    EXPECT_NEAR(sum_position_qty(positions), 1.0, 1e-12);

    const auto second = AccountPolicyExecutionService::ApplyUpdates(
        oneKline("BTCUSDT", 100.0, 1.0),
        {},
        policies,
        0,
        {},
        open_orders,
        positions);
    EXPECT_EQ(second.filled_count, 1u);
    EXPECT_TRUE(open_orders.empty());
    EXPECT_NEAR(sum_position_qty(positions), 2.0, 1e-12);
}

TEST(AccountPolicyExecutionServiceTest, TickVolumeIsConsumedAcrossOrdersSameSymbol)
{
    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};

    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        6.0,
        100.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));
    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        6.0,
        100.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    const AccountPolicies policies = AccountPolicies::Default();
    const auto result = AccountPolicyExecutionService::ApplyUpdates(
        oneKline("BTCUSDT", 100.0, 10.0),
        {},
        policies,
        0,
        {},
        open_orders,
        positions);

    EXPECT_EQ(result.filled_count, 2u);
    EXPECT_NEAR(sum_position_qty(positions), 10.0, 1e-12);
    ASSERT_EQ(open_orders.size(), 1u);
    EXPECT_NEAR(open_orders[0].quantity, 2.0, 1e-12);
}

