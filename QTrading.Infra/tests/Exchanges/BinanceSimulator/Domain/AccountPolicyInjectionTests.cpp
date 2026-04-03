#include <gtest/gtest.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"
#include "Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionService.hpp"

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;
using QTrading::dto::Order;
using QTrading::Infra::Exchanges::BinanceSim::AccountPerSymbolMarketContext;
using QTrading::Infra::Exchanges::BinanceSim::AccountPolicies;
using QTrading::Infra::Exchanges::BinanceSim::Domain::AccountPolicyExecutionService;

namespace {

std::unordered_map<std::string, TradeKlineDto> one_kline(
    const std::string& symbol,
    double open,
    double high,
    double low,
    double close,
    double volume)
{
    TradeKlineDto kline{};
    kline.OpenPrice = open;
    kline.HighPrice = high;
    kline.LowPrice = low;
    kline.ClosePrice = close;
    kline.Volume = volume;
    return { { symbol, kline } };
}

} // namespace

TEST(AccountPoliciesTest, InjectedExecutionPriceIsUsed)
{
    AccountPolicies policies = AccountPolicies::Default();
    policies.execution_price_ctx = [](const Order&, const AccountPerSymbolMarketContext&, double, double) {
        return 123.0;
    };

    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};
    std::unordered_map<std::string, double> symbol_leverage{ { "BTCUSDT", 10.0 } };
    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        1.0,
        0.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    (void)AccountPolicyExecutionService::ApplyUpdates(
        one_kline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 1000.0),
        {},
        policies,
        0,
        symbol_leverage,
        open_orders,
        positions);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 123.0, 1e-12);
}

TEST(AccountPoliciesTest, InjectedFeeRatesAffectChargedFeeRate)
{
    AccountPolicies policies = AccountPolicies::Default();
    policies.fee_rates = [](int) {
        return std::make_tuple(0.10, 0.20);
    };
    policies.can_fill_and_taker_ctx = [](const Order&, const AccountPerSymbolMarketContext&) {
        return std::make_pair(true, true);
    };

    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};
    std::unordered_map<std::string, double> symbol_leverage{ { "BTCUSDT", 10.0 } };
    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        1.0,
        0.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    const auto result = AccountPolicyExecutionService::ApplyUpdates(
        one_kline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0),
        {},
        policies,
        0,
        symbol_leverage,
        open_orders,
        positions);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].fee_rate, 0.20, 1e-12);
    EXPECT_NEAR(result.perp_wallet_delta, -20.0, 1e-12);
}

TEST(AccountPoliciesTest, ContextExecutionPriceSeesMarkPrice)
{
    AccountPolicies policies = AccountPolicies::Default();

    bool context_called = false;
    std::optional<double> observed_mark{};
    policies.execution_price_ctx = [&](const Order&, const AccountPerSymbolMarketContext& ctx, double, double) {
        context_called = true;
        observed_mark = ctx.last_mark_price;
        return ctx.last_mark_price.value_or(0.0);
    };

    int next_order_id = 1;
    std::vector<QTrading::dto::Order> open_orders{};
    std::vector<QTrading::dto::Position> positions{};
    std::unordered_map<std::string, double> symbol_leverage{ { "BTCUSDT", 10.0 } };
    ASSERT_TRUE(AccountPolicyExecutionService::TryQueueOrder(
        "BTCUSDT",
        1.0,
        0.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        next_order_id,
        open_orders));

    const std::unordered_map<std::string, double> mark_price{ { "BTCUSDT", 250.0 } };
    (void)AccountPolicyExecutionService::ApplyUpdates(
        one_kline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 1000.0),
        mark_price,
        policies,
        0,
        symbol_leverage,
        open_orders,
        positions);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 250.0, 1e-12);
    EXPECT_TRUE(context_called);
    ASSERT_TRUE(observed_mark.has_value());
    EXPECT_NEAR(*observed_mark, 250.0, 1e-12);
}
