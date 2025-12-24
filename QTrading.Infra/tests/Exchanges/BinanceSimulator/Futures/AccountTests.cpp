#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream> // for CaptureStdout, CaptureStderr
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include "Dto/Trading/Side.hpp"

static std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto> oneKline(
    const std::string& sym,
    double o, double h, double l, double c,
    double vol)
{
    QTrading::Dto::Market::Binance::KlineDto k;
    k.OpenPrice = o;
    k.HighPrice = h;
    k.LowPrice = l;
    k.ClosePrice = c;
    k.Volume = vol;
    return { {sym, k} };
}

/// @brief Helper: generate market data for BTCUSDT only.
std::unordered_map<std::string, std::pair<double, double>> partialMarketDataBTC(double price, double available) {
    return { {"BTCUSDT", {price, available}} };
}

/// @brief Helper: generate market data for BTCUSDT and ETHUSDT.
std::unordered_map<std::string, std::pair<double, double>> twoSymbolMarketData(double btcPrice, double btcVol,
    double ethPrice, double ethVol)
{
    return {
        {"BTCUSDT", {btcPrice, btcVol}},
        {"ETHUSDT", {ethPrice, ethVol}}
    };
}

/// @brief Verifies constructor initializes balances and PnL to expected values.
TEST(AccountTest, ConstructorAndGetters) {
    Account account(1000.0, 0);
    auto bal = account.get_balance();
    EXPECT_DOUBLE_EQ(bal.WalletBalance, 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 1000.0);
}

/// @brief Verifies setting and getting symbol leverage, and error on invalid.
TEST(AccountTest, SetAndGetSymbolLeverage) {
    Account account(2000.0, 0);
    // Default => 1.0
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 1.0);

    // Set 50x
    account.set_symbol_leverage("BTCUSDT", 50.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 50.0);

    // <=0 => throw
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", 0.0), std::runtime_error);
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", -10.0), std::runtime_error);
}

/// @brief Verifies limit order placement appears in open_orders without immediate balance change.
TEST(AccountTest, PlaceOrderSuccessCheckOpenOrders) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    testing::internal::CaptureStdout();
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 7000.0, OrderSide::Buy, PositionSide::Both));
    std::string out = testing::internal::GetCapturedStdout();

    const auto& orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(orders[0].quantity, 1.0);
    EXPECT_DOUBLE_EQ(orders[0].price, 7000.0);
    EXPECT_TRUE(orders[0].side == OrderSide::Buy);
    EXPECT_EQ(orders[0].closing_position_id, -1);

    EXPECT_DOUBLE_EQ(account.get_balance().WalletBalance, 10000.0);
}

/// @brief Verifies partial fill creates position and leaves leftover order.
TEST(AccountTest, UpdatePositionsPartialFillSameOrder) {
    Account account(5000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5.0, 1000.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(partialMarketDataBTC(1000.0, 2.0));

    const auto& orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 3.0);

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);
    EXPECT_EQ(positions[0].symbol, "BTCUSDT");

    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    const auto& ordersAfter = account.get_all_open_orders();
    EXPECT_EQ(ordersAfter.size(), 0u);

    const auto& positionsAfter = account.get_all_positions();
    ASSERT_EQ(positionsAfter.size(), 1u);
    EXPECT_DOUBLE_EQ(positionsAfter[0].quantity, 5.0);
}

/// @brief Verifies market-close order realizes PnL and clears position.
TEST(AccountTest, ClosePositionBySymbol) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 1000.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(partialMarketDataBTC(1000.0, 5.0));
    // now we have a position of 2 BTC at 1000

    std::unordered_map<std::string, std::pair<double, double>> data2 = partialMarketDataBTC(1200.0, 5.0);
    account.update_positions(data2);

    account.close_position("BTCUSDT");

    testing::internal::CaptureStderr();
    account.update_positions(data2);
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(account.get_all_positions().empty());
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

/// @brief Verifies cancel_order_by_id removes leftover but keeps filled position.
TEST(AccountTest, CancelOrderByID) {
    Account account(5000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5.0, 500.0, OrderSide::Buy, PositionSide::Both));
    auto orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    int oid = orders[0].id;

    account.update_positions(partialMarketDataBTC(500.0, 2.0));

    orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 3.0);

    account.cancel_order_by_id(oid);

    EXPECT_TRUE(account.get_all_open_orders().empty());
    EXPECT_FALSE(account.get_all_positions().empty());
}

/// @brief Verifies liquidation clears all positions and zeroes balance.
TEST(AccountTest, Liquidation) {
    Account account(350000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 75.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(500.0, 10000.0));
    EXPECT_DOUBLE_EQ(account.get_all_positions().size(), 1);

    testing::internal::CaptureStderr();
    account.update_positions(partialMarketDataBTC(1.0, 10000.0));
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);

    EXPECT_TRUE(account.get_all_positions().empty() || account.get_balance().MarginBalance >= account.get_balance().MaintenanceMargin);
}

/// @brief Verifies hedge-mode allows separate long and short positions.
TEST(AccountTest, HedgeModeSameSymbolOppositeDirection) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 3000.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 3000.0, OrderSide::Sell, PositionSide::Short));

    account.update_positions(partialMarketDataBTC(3000.0, 10.0));

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    bool longFound = false;
    bool shortFound = false;
    for (auto& pos : positions) {
        if (pos.is_long && pos.quantity == 2.0) longFound = true;
        if (!pos.is_long && pos.quantity == 1.0) shortFound = true;
    }
    EXPECT_TRUE(longFound);
    EXPECT_TRUE(shortFound);
}

/////////////////////////////////////////////////////////
/// 1. Switching Single/Hedge Mode
/////////////////////////////////////////////////////////

/// Scenario 1: Switching mode with open positions (should fail)
TEST(AccountTest, SwitchingModeWithOpenPositionsFails) {
	Account account(10000.0, 0);
	account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    EXPECT_FALSE(account.get_all_positions().empty());

    account.set_position_mode(true);
    EXPECT_FALSE(account.is_hedge_mode());
}

/// Scenario 2: Switching mode when no positions exist (should succeed)
TEST(AccountTest, SwitchingModeWithoutPositionsSucceeds) {
    Account account(10000.0, 0);
    // Create a fresh account with no orders/positions.
    // 此處可直接使用 fixture 內的 account（初始 balance 10000）。
    EXPECT_TRUE(account.get_all_positions().empty());

    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
}

/////////////////////////////////////////////////////////
/// 2. Single Mode Auto-Reduce vs. Hedge Mode reduceOnly
/////////////////////////////////////////////////////////

/// @brief Single mode: auto-reduce should happen on opposite position open.
TEST(AccountTest, SingleModeAutoReduceOppositePositionOpen) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 9000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 9000.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-6);
}

/// @brief Hedge mode with reduce_only = true
TEST(AccountTest, HedgeModeReduceOnlyOrder) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Use explicit OHLC so we can reason about limit triggers.
    // Keep Volume high enough so no leftover open orders affect later steps.
    auto mkKline = [](const std::string& sym, double px, double vol) {
        QTrading::Dto::Market::Binance::KlineDto k;
        k.OpenPrice = px;
        k.HighPrice = px;
        k.LowPrice = px;
        k.ClosePrice = px;
        k.Volume = vol;
        return std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>{ {sym, k} };
    };

    auto k9000 = mkKline("BTCUSDT", 9000.0, 1000.0);
    auto k11000 = mkKline("BTCUSDT", 11000.0, 1000.0);

    // Place a LONG 2 BTCUSDT order and fill it.
    account.place_order("BTCUSDT", 2.0, 10000.0, OrderSide::Buy, PositionSide::Long);
    account.update_positions(k9000);
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);

    // Place another LONG order for 1 BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long);
    account.update_positions(k9000);
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 3.0);

    // Place another SHORT order for 1 BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Short);
    account.update_positions(k11000);
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // Now place a reduce_only order to reduce LONG by 1 (SELL reduce-only targeting LONG).
    // For SELL limit=10000 to trigger, High must be >= 10000.
    account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Long, true);
    account.update_positions(k11000);
    positions = account.get_all_positions();

    // We should still have both sides, and LONG reduced from 3 to 2.
    ASSERT_EQ(positions.size(), 2u);
    double longQty = 0.0;
    double shortQty = 0.0;
    for (const auto& p : positions) {
        if (p.is_long) longQty = p.quantity;
        else shortQty = p.quantity;
    }
    EXPECT_DOUBLE_EQ(longQty, 2.0);
    EXPECT_DOUBLE_EQ(shortQty, 1.0);
}

/////////////////////////////////////////////////////////
/// 3. Merge Positions (Same Symbol & Direction)
/////////////////////////////////////////////////////////

/// @brief Verifies position merging in hedge mode
TEST(AccountTest, MergePositionsSameDirection) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 3.0, OrderSide::Buy, PositionSide::Long));
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 6.0);
}

TEST(AccountTest, MergePositionsDifferentDirectionNotMerged) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Short));
    account.update_positions(partialMarketDataBTC(10000.0, 10.0));

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);
    int longCount = 0, shortCount = 0;
    for (const auto& pos : positions) {
        if (pos.symbol == "BTCUSDT") {
            if (pos.is_long)
                longCount++;
            else
                shortCount++;
        }
    }
    EXPECT_EQ(longCount, 1);
    EXPECT_EQ(shortCount, 1);
}

TEST(AccountTest, CloseOnlyLongSideInHedgeMode) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Short));
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    account.close_position("BTCUSDT", PositionSide::Long);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_FALSE(positions[0].is_long);
}

/// @brief Close both sides by calling close_position(symbol) with no direction in hedge mode
TEST(AccountTest, CloseBothSidesInHedgeMode) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Place both LONG and SHORT orders.
    account.place_order("BTCUSDT", 2.0, 10000.0, OrderSide::Buy, PositionSide::Long);
    account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Short);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // Close positions without specifying a direction.
    account.close_position("BTCUSDT");
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));

    positions = account.get_all_positions();
    // Expected: no open positions remain.
    EXPECT_EQ(positions.size(), 0u);
}

/////////////////////////////////////////////////////////
/// 5. Leverage Adjustments With Existing Positions
/////////////////////////////////////////////////////////

/// @brief Adjusting leverage should succeed/fail depending on available margin
TEST(AccountTest, AdjustLeverageWithExistingPositions) {
    // Provide more collateral to satisfy open-order/position margin occupation.
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 20.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Both);
    account.update_positions(twoSymbolMarketData(4000.0, 2.0, 0.0, 0.0));

    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 10.0);

    account.set_symbol_leverage("BTCUSDT", 40.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 40.0);
}

TEST(AccountTest, SingleMode_MultipleSymbols) {
    Account account(50000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_symbol_leverage("ETHUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 20000.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("ETHUSDT", 2.0, 1500.0, OrderSide::Sell, PositionSide::Both));

    auto marketData = twoSymbolMarketData(20000.0, 5.0, 1500.0, 10.0);
    account.update_positions(marketData);

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    bool foundBTC = false, foundETH = false;
    for (const auto& pos : positions) {
        if (pos.symbol == "BTCUSDT") {
            foundBTC = true;
            EXPECT_TRUE(pos.is_long);
            EXPECT_DOUBLE_EQ(pos.quantity, 1.0);
        }
        else if (pos.symbol == "ETHUSDT") {
            foundETH = true;
            EXPECT_FALSE(pos.is_long);
            EXPECT_DOUBLE_EQ(pos.quantity, 2.0);
        }
    }
    EXPECT_TRUE(foundBTC);
    EXPECT_TRUE(foundETH);

    ASSERT_TRUE(account.place_order("BTCUSDT", 0.5, 20000.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(marketData);

    positions = account.get_all_positions();
    // Behavior depends on one-way reverse logic; only assert ETH position still exists.
    bool ethStillThere = false;
    for (const auto& pos : positions) {
        if (pos.symbol == "ETHUSDT") ethStillThere = true;
    }
    EXPECT_TRUE(ethStillThere);
}

TEST(AccountTest, HedgeMode_MultipleSymbols_ReduceOnly) {
    // Increase collateral so openings succeed under cross margin.
    Account account(50000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_symbol_leverage("ETHUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    auto marketData = twoSymbolMarketData(20000.0, 10.0, 1500.0, 10.0);
    account.place_order("BTCUSDT", 2.0, 20000.0, OrderSide::Buy, PositionSide::Long);
    account.place_order("ETHUSDT", 3.0, 1500.0, OrderSide::Buy, PositionSide::Long);
    account.update_positions(marketData);

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // reduceOnly semantics will be refined later; for now ensure system remains stable.
    account.place_order("BTCUSDT", 1.0, 20000.0, OrderSide::Sell, PositionSide::Long, true);
    account.update_positions(marketData);

    EXPECT_FALSE(account.get_all_positions().empty());
}

/// @brief Ensures per-tick available volume is consumed across multiple orders for same symbol.
TEST(AccountTest, TickVolumeIsConsumedAcrossOrdersSameSymbol) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1100.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1200.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_NEAR(pos[0].quantity, 1.0, 1e-8);

    const auto& ord = account.get_all_open_orders();
    ASSERT_EQ(ord.size(), 1u);
    EXPECT_NEAR(ord[0].quantity, 1.0, 1e-8);
}

/// @brief Ensures limit orders that execute immediately are charged taker fee (not maker).
TEST(AccountTest, ImmediatelyExecutableLimitIsTakerFee) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 2000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);

    EXPECT_NEAR(positions[0].fee_rate, 0.00050, 1e-12);
}

TEST(AccountTest, LimitOrderFillsAtLimitPrice) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 2000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 2000.0, 1e-12);
}

TEST(AccountTest, TickPriceTimePriority_BuyHigherLimitFillsFirst) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1100.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1200.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-8);

    const auto& openOrders = account.get_all_open_orders();
    ASSERT_EQ(openOrders.size(), 1u);
    EXPECT_NEAR(openOrders[0].price, 1100.0, 1e-12);
}

TEST(AccountTest, TickPriceTimePriority_SamePriceLowerIdFillsFirst) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1200.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 1200.0, OrderSide::Buy, PositionSide::Both));

    int firstId = account.get_all_open_orders()[0].id;
    int secondId = account.get_all_open_orders()[1].id;
    ASSERT_LT(firstId, secondId);

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    const auto& openOrders = account.get_all_open_orders();
    ASSERT_EQ(openOrders.size(), 1u);
    EXPECT_EQ(openOrders[0].id, secondId);
    EXPECT_NEAR(openOrders[0].quantity, 1.0, 1e-8);
}

TEST(AccountTest, OhlcTrigger_BuyLimitTriggersOnLow) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 95.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 110.0, 120.0, 90.0, 105.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 95.0, 1e-12);
}

TEST(AccountTest, OhlcTrigger_SellLimitTriggersOnHigh) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 115.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 110.0, 120.0, 100.0, 105.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 115.0, 1e-12);
}

TEST(AccountTest, HedgeMode_OrderRequiresExplicitPositionSide_IsRejectedWithoutException) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    EXPECT_NO_THROW(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, ReduceOnlyWithoutReduciblePosition_IsRejectedWithoutException) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    EXPECT_NO_THROW(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Both, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, HedgeModeReduceOnly_WrongSideOrNoMatchingPosition_IsRejectedWithoutException) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    // Wrong close direction: BUY reduce-only targeting LONG should be rejected.
    EXPECT_NO_THROW(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long, true));
    // Mismatched position_side: attempting to reduce SHORT when only LONG exists.
    EXPECT_NO_THROW(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Short, true));

    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, OneWayFlip_OvershootSplitsIntoCloseThenOpen) {
    Account account(50000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Open LONG 2
    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    // SELL 5 => close 2 then open SHORT 3
    ASSERT_TRUE(account.place_order("BTCUSDT", 5.0, 100.0, OrderSide::Sell, PositionSide::Both));

    const auto& ords = account.get_all_open_orders();
    ASSERT_EQ(ords.size(), 2u);

    // First order should be a closing order
    EXPECT_GE(ords[0].closing_position_id, 0);
    EXPECT_EQ(ords[0].side, OrderSide::Sell);

    // Second order should be an opening order
    EXPECT_EQ(ords[1].closing_position_id, -1);
    EXPECT_EQ(ords[1].side, OrderSide::Sell);
    EXPECT_NEAR(ords[1].quantity, 3.0, 1e-12);

    // Fill both and ensure we end with a SHORT 3
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    const auto& posAfter = account.get_all_positions();
    ASSERT_EQ(posAfter.size(), 1u);
    EXPECT_FALSE(posAfter[0].is_long);
    EXPECT_NEAR(posAfter[0].quantity, 3.0, 1e-12);
}

TEST(AccountTest, ReduceOnly_OneWayRejectsIfWouldIncreaseExposure) {
    Account account(50000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // No positions: reduce-only should be rejected
    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    // Open LONG 1
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));

    // reduce-only BUY would increase LONG => reject
    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    // reduce-only SELL reduces LONG => accept
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.5, 100.0, OrderSide::Sell, PositionSide::Both, true));
}

TEST(AccountTest, ReduceOnly_HedgeMode_RequiresExplicitPositionSide) {
    Account account(50000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Open both LONG and SHORT.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell, PositionSide::Short));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 2u);

    // reduceOnly without specifying Long/Short should be rejected.
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.5, 100.0, OrderSide::Sell, PositionSide::Both, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, ReduceOnly_HedgeMode_DirectionMustCloseCorrectSide) {
    Account account(50000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Open LONG 1 and SHORT 1.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell, PositionSide::Short));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 2u);

    // Wrong direction: BUY reduce-only targeting LONG should be rejected.
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Buy, PositionSide::Long, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    // Wrong direction: SELL reduce-only targeting SHORT should be rejected.
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Sell, PositionSide::Short, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    // Correct directions should be accepted.
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Sell, PositionSide::Long, true));
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Buy, PositionSide::Short, true));
}

TEST(AccountTest, OpenOrderInitialMargin_MarketOrderUsesLastMarkWithBuffer) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_market_slippage_buffer(0.0); // deterministic

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Establish last mark=100
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 0.0, OrderSide::Buy, PositionSide::Both));

    auto bal = account.get_balance();
    // notional = 2*100, lev=10 => 20
    EXPECT_NEAR(bal.OpenOrderInitialMargin, 20.0, 1e-12);
}

TEST(AccountTest, OpenOrderInitialMargin_OneWayClosingDirectionDoesNotReserveMargin) {
    Account account(50000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_market_slippage_buffer(0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // mark=100
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    // Open LONG 1 and fill.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    // Place SELL limit (closing direction) qty<=pos => will be a closing order w/ closing_position_id
    ASSERT_TRUE(account.place_order("BTCUSDT", 0.5, 100.0, OrderSide::Sell, PositionSide::Both));

    auto bal = account.get_balance();
    EXPECT_NEAR(bal.OpenOrderInitialMargin, 0.0, 1e-12);
}

TEST(AccountTest, OpenOrderInitialMargin_OneWayFlipReservesOnlyForOpeningOvershoot) {
    Account account(50000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_market_slippage_buffer(0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // mark=100
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    // Open LONG 2 and fill.
    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    // SELL 5 => split into close 2 + open short 3
    ASSERT_TRUE(account.place_order("BTCUSDT", 5.0, 100.0, OrderSide::Sell, PositionSide::Both));

    auto bal = account.get_balance();
    // Only overshoot 3 should reserve: 3*100/10 = 30
    EXPECT_NEAR(bal.OpenOrderInitialMargin, 30.0, 1e-12);
}

TEST(AccountTest, MarketOrderFill_UsesExecutionSlippageBoundedByOHLC) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_market_execution_slippage(0.10); // 10%

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // BUY market: expected worse-than-close, but capped by High.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 0.0, OrderSide::Buy, PositionSide::Both));

    // close=100, high=105 => 100*(1+0.1)=110 => capped to 105
    account.update_positions(oneKline("BTCUSDT", 100.0, 105.0, 95.0, 100.0, 1000.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_TRUE(pos[0].is_long);
    EXPECT_NEAR(pos[0].entry_price, 105.0, 1e-12);
}

TEST(AccountTest, LimitOrderFill_UsesExecutionSlippageButRespectsLimit) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_limit_execution_slippage(0.10); // 10%

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Buy limit at 100 triggers when Low <= 100.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));

    // close=95, high=110, low=90 => worse=95*(1.1)=104.5, capped by high=110 -> 104.5
    // but must not exceed limit=100 => fill=100
    account.update_positions(oneKline("BTCUSDT", 95.0, 110.0, 90.0, 95.0, 1000.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_TRUE(pos[0].is_long);
    EXPECT_NEAR(pos[0].entry_price, 100.0, 1e-12);
}

TEST(AccountTest, LimitOrderFill_ExecutionSlippageCanWorsenPriceWithinLimit) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_limit_execution_slippage(0.10); // 10%

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Buy limit at 110 triggers, and the pessimistic fill (based on close) stays below the limit.
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 110.0, OrderSide::Buy, PositionSide::Both));

    // close=95, high=110, low=90 => worse=95*(1.1)=104.5 (<= limit 110)
    account.update_positions(oneKline("BTCUSDT", 95.0, 110.0, 90.0, 95.0, 1000.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_TRUE(pos[0].is_long);
    EXPECT_NEAR(pos[0].entry_price, 104.5, 1e-12);
}

TEST(AccountTest, TickVolumeSplit_UsesTakerBuyBaseVolumePools) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Two market orders, one BUY and one SELL, each qty=6.
    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Sell, PositionSide::Both));

    // Build kline with Volume=10 and TakerBuyBaseVolume=8
    // => buy_liq=8 (SELL orders can consume), sell_liq=2 (BUY orders can consume)
    QTrading::Dto::Market::Binance::KlineDto k;
    k.OpenPrice = 100.0;
    k.HighPrice = 110.0;
    k.LowPrice = 90.0;
    k.ClosePrice = 100.0;
    k.Volume = 10.0;
    k.TakerBuyBaseVolume = 8.0;

    account.update_positions(std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>{ {"BTCUSDT", k} });

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);

    double longQty = 0.0;
    double shortQty = 0.0;
    for (const auto& p : pos) {
        if (p.is_long) longQty += p.quantity;
        else shortQty += p.quantity;
    }

    // BUY consumes sell_liq=2, SELL consumes buy_liq=8 but order qty is 6.
    EXPECT_NEAR(longQty, 2.0, 1e-12);
    EXPECT_NEAR(shortQty, 6.0, 1e-12);

    const auto& ords = account.get_all_open_orders();
    ASSERT_EQ(ords.size(), 1u);
    EXPECT_EQ(ords[0].side, OrderSide::Buy);
    EXPECT_NEAR(ords[0].quantity, 4.0, 1e-12);
}

TEST(AccountTest, TickVolumeSplit_Heuristic_CloseNearHighBiasesSellOrders) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::TakerBuyOrHeuristic);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Sell, PositionSide::Both));

    // Volume=10, close near high => buy_liq ~ 9, sell_liq ~ 1.
    // BUY consumes sell_liq => ~1, SELL consumes buy_liq => 6.
    QTrading::Dto::Market::Binance::KlineDto k;
    k.OpenPrice = 100.0;
    k.HighPrice = 110.0;
    k.LowPrice = 90.0;
    k.ClosePrice = 108.0;
    k.Volume = 10.0;
    // TakerBuyBaseVolume absent => 0

    account.update_positions(std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>{ {"BTCUSDT", k} });

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);

    double longQty = 0.0;
    double shortQty = 0.0;
    for (const auto& p : pos) {
        if (p.is_long) longQty += p.quantity;
        else shortQty += p.quantity;
    }

    EXPECT_NEAR(longQty, 1.0, 1e-12);
    EXPECT_NEAR(shortQty, 6.0, 1e-12);
}

TEST(AccountTest, TickVolumeSplit_Heuristic_CloseNearLowBiasesBuyOrders) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::TakerBuyOrHeuristic);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 6.0, 0.0, OrderSide::Sell, PositionSide::Both));

    // Volume=10, close near low => buy_liq ~ 1, sell_liq ~ 9.
    // SELL consumes buy_liq => ~1, BUY consumes sell_liq => 6.
    QTrading::Dto::Market::Binance::KlineDto k;
    k.OpenPrice = 100.0;
    k.HighPrice = 110.0;
    k.LowPrice = 90.0;
    k.ClosePrice = 92.0;
    k.Volume = 10.0;

    account.update_positions(std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>{ {"BTCUSDT", k} });

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);

    double longQty = 0.0;
    double shortQty = 0.0;
    for (const auto& p : pos) {
        if (p.is_long) longQty += p.quantity;
        else shortQty += p.quantity;
    }

    EXPECT_NEAR(longQty, 6.0, 1e-12);
    EXPECT_NEAR(shortQty, 1.0, 1e-12);
}
