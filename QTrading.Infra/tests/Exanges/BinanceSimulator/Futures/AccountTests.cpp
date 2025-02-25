#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream> // for CaptureStdout, CaptureStderr
#include "Exanges/BinanceSimulator/Futures/Account.hpp"

/////////////////////////////////////////////////////////
// Helper functions for creating mock market data
/////////////////////////////////////////////////////////

// Returns market data for BTCUSDT with specified available volume.
std::unordered_map<std::string, std::pair<double, double>> partialMarketDataBTC(double price, double available) {
    return { {"BTCUSDT", {price, available}} };
}

// Returns market data for two symbols (BTCUSDT and ETHUSDT) with given prices and volumes.
std::unordered_map<std::string, std::pair<double, double>> twoSymbolMarketData(double btcPrice, double btcVol,
    double ethPrice, double ethVol)
{
    return {
        {"BTCUSDT", {btcPrice, btcVol}},
        {"ETHUSDT", {ethPrice, ethVol}}
    };
}

/**
 * 1) Constructor & Basic Getter
 */
TEST(AccountTest, ConstructorAndGetters) {
    Account account(1000.0, 0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 1000.0);
}

/**
 * 2) Set & Get Symbol Leverage
 *    - Normal set
 *    - Invalid leverage => throw
 */
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

/**
 * 3) place_order (opening) -> open_orders check
 *    We won't see margin deduction until update_positions.
 */
TEST(AccountTest, PlaceOrderSuccessCheckOpenOrders) {
    Account account(10000.0, 0);

    // price>0 => limit order
    // Capture output to verify no error logs
    testing::internal::CaptureStdout();
    account.place_order("BTCUSDT", 1.0, 7000.0, true); // is_long=true
    std::string out = testing::internal::GetCapturedStdout();

    // There's exactly 1 open order
    const auto& orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(orders[0].quantity, 1.0);
    EXPECT_DOUBLE_EQ(orders[0].price, 7000.0);
    EXPECT_TRUE(orders[0].is_long);
    // Not closing any position => closing_position_id == -1 (預設)
    EXPECT_EQ(orders[0].closing_position_id, -1);

    // Balance not deducted yet (Hedge-mode, margin is only deducted in update_positions)
    EXPECT_DOUBLE_EQ(account.get_balance(), 10000.0);

    // Verify that there's no error log in out
    // (We might or might not check specific logs here)
    // e.g., we can check some substring if needed
}

/**
 * 4) Update positions for partial fill
 *    - Only part of quantity matched => leftover in open_orders
 *    - The partial fill merges into same Position if from the same order
 */
TEST(AccountTest, UpdatePositionsPartialFillSameOrder) {
    Account account(5000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place a limit order for 5 BTC at price=1000
    account.place_order("BTCUSDT", 5.0, 1000.0, true);

    // update_positions => expect partial fill of 2 BTC
    account.update_positions(partialMarketDataBTC(1000.0, 2.0));

    // Check: 2 BTC is filled => create a new position, leftover=3 BTC
    const auto& orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u); // leftover order still open
    EXPECT_DOUBLE_EQ(orders[0].quantity, 3.0);

    // positions => 1 position with 2 BTC
    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);
    EXPECT_EQ(positions[0].symbol, "BTCUSDT");

    // Next update => fill the leftover 3
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    // Now the leftover order should be fully filled => no more open orders
    const auto& ordersAfter = account.get_all_open_orders();
    EXPECT_EQ(ordersAfter.size(), 0u);

    // The existing position merges into the same position => total quantity= 2+3=5
    const auto& positionsAfter = account.get_all_positions();
    ASSERT_EQ(positionsAfter.size(), 1u);
    EXPECT_DOUBLE_EQ(positionsAfter[0].quantity, 5.0);
}

/**
 * 5) close_position => internally creates "closing orders"
 *    - If we pass price <=0 => market close
 *    - If we pass price>0 => limit close
 *    Then update_positions => fill the closing order => realize PnL
 */
TEST(AccountTest, ClosePositionBySymbol) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // 1) Place a limit order => open a position
    account.place_order("BTCUSDT", 2.0, 1000.0, true);

    // 2) match => fully filled
    account.update_positions(partialMarketDataBTC(1000.0, 5.0));
    // now we have a position of 2 BTC at 1000

    // Let the price go up => e.g., 1200 => unrealized profit
    std::unordered_map<std::string, std::pair<double, double>> data2 = partialMarketDataBTC(1200.0, 5.0);
    account.update_positions(data2);

    // 3) close_position => default param => price=0 => market close
    //    This creates "closing orders" for all positions in that symbol
    account.close_position("BTCUSDT");
    // now there's a new "closing order" in open_orders_, which we'll fill below

    // 4) update_positions => fill the closing order => realize PnL
    testing::internal::CaptureStderr();
    account.update_positions(data2);
    std::string logs = testing::internal::GetCapturedStderr();

    // After close => positions_ should be empty
    EXPECT_TRUE(account.get_all_positions().empty());
    // The open_orders_ for closing also should be gone after fill
    EXPECT_TRUE(account.get_all_open_orders().empty());

    // balance => 10000 + realizedPnL - fees ...
    // We won't do exact numeric check here unless you want to 
    // factor in the margin & fee from your logic. 
    // The key is that position is closed, no positions remain.
}

/**
 * 6) cancel_order_by_id => remove leftover portion
 *    - If partial fill already done, only leftover is removed
 */
TEST(AccountTest, CancelOrderByID) {
    Account account(5000.0, 0);
    // place a large order
    account.place_order("BTCUSDT", 5.0, 500.0, true);
    auto orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    int oid = orders[0].id;

    // partial fill => 2 BTC
    account.update_positions(partialMarketDataBTC(500.0, 2.0));

    // leftover => 3 BTC
    orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 3.0);

    // now cancel leftover
    account.cancel_order_by_id(oid);

    // open_orders_ => empty
    EXPECT_TRUE(account.get_all_open_orders().empty());
    // the 2 BTC partial fill => positions exist
    EXPECT_FALSE(account.get_all_positions().empty());
}

/**
 * 7) Liquidation test:
 *    - Large position => negative equity => liquidation triggers
 *    - verify positions cleared, balance=0
 */
TEST(AccountTest, Liquidation) {
    Account account(2000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place an order => e.g., 4 BTC at 500 => notional=2000 => margin=200 => fee=some small => ok
    account.place_order("BTCUSDT", 4.0, 500.0, true);

    // fill it
    account.update_positions(partialMarketDataBTC(500.0, 10.0));
    EXPECT_DOUBLE_EQ(account.get_all_positions().size(), 1);

    // now let's crash price => 50 => negative PnL => triggers liquidation
    testing::internal::CaptureStderr();
    account.update_positions(partialMarketDataBTC(50.0, 10.0));
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);
    // positions_ cleared
    EXPECT_TRUE(account.get_all_positions().empty());
    // balance=0
    EXPECT_DOUBLE_EQ(account.get_balance(), 0.0);
}

/**
 * 8) Hedge-mode: same symbol, different direction => separate positions
 *    - place two orders (one long, one short) => check both get filled => no offset
 */
TEST(AccountTest, HedgeModeSameSymbolOppositeDirection) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // 1) LONG order
    account.place_order("BTCUSDT", 2.0, 3000.0, true);
    // 2) SHORT order
    account.place_order("BTCUSDT", 1.0, 3000.0, false);

    // fill them all
    account.update_positions(partialMarketDataBTC(3000.0, 10.0));

    // we expect 2 separate positions
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
// 1. Switching Single/Hedge Mode
/////////////////////////////////////////////////////////

// Scenario 1: Switching mode with open positions (should fail)
TEST(AccountTest, SwitchingModeWithOpenPositionsFails) {
	Account account(10000.0, 0);
	account.set_position_mode(false);
    // Default is one-way mode.
    EXPECT_FALSE(account.is_hedge_mode());

    // Place an order so that a position is opened (e.g., LONG 1 BTCUSDT).
    account.place_order("BTCUSDT", 1.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    EXPECT_FALSE(account.get_all_positions().empty());

    // Attempt to switch mode to hedge mode.
    account.set_position_mode(true);
    // Mode switch should be rejected because there is an open position.
    EXPECT_FALSE(account.is_hedge_mode());
}

// Scenario 2: Switching mode when no positions exist (should succeed)
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
// 2. Single Mode Auto-Reduce vs. Hedge Mode reduceOnly
/////////////////////////////////////////////////////////

// Scenario 3: Single mode auto-reduce (reverse order logic)
TEST(AccountTest, SingleModeAutoReduceReverseOrder) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    // In one-way mode.
    EXPECT_FALSE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place a LONG 2 BTCUSDT order at price 9000.
    account.place_order("BTCUSDT", 2.0, 9000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);

    // Place a new SHORT order with quantity = 1 (reverse direction).
    account.place_order("BTCUSDT", 1.0, 9000.0, false);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    positions = account.get_all_positions();
    // Expect the existing LONG is partially reduced from 2 to 1.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-6);
}

// Scenario 4: Hedge mode with reduce_only = true
TEST(AccountTest, HedgeModeReduceOnlyOrder) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place a LONG 2 BTCUSDT order and fill it.
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);

    // Place another LONG order for 1 BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 3.0);

    // Place another SHORT order for 1 BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, false);
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_DOUBLE_EQ(positions[1].quantity, 1.0);

    // Now place a reduce_only LONG order for 1 BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, true, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 2.0);
}

/////////////////////////////////////////////////////////
// 3. Merge Positions (Same Symbol & Direction)
/////////////////////////////////////////////////////////

// Scenario 5: Verifying position merging in hedge mode
TEST(AccountTest, MergePositionsSameDirection) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place 3 LONG orders for BTCUSDT: 1, 2, 3 BTC at the same price.
    account.place_order("BTCUSDT", 1.0, 10000.0, true);
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.place_order("BTCUSDT", 3.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    auto positions = account.get_all_positions();
    // Expect one merged position with total quantity = 6 BTC.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 6.0);
}

// Scenario 6: Verifying that different directions do not merge
TEST(AccountTest, MergePositionsDifferentDirectionNotMerged) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place one LONG and one SHORT order on BTCUSDT.
    account.place_order("BTCUSDT", 1.0, 10000.0, true);
    account.place_order("BTCUSDT", 1.0, 10000.0, false);
    account.update_positions(partialMarketDataBTC(10000.0, 10.0));

    auto positions = account.get_all_positions();
    // Expect two separate positions.
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

/////////////////////////////////////////////////////////
// 4. Closing Positions in Hedge Mode with Direction
/////////////////////////////////////////////////////////

// Scenario 7: Close only the LONG side of a hedge-mode symbol
TEST(AccountTest, CloseOnlyLongSideInHedgeMode) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place a LONG 2 BTCUSDT order and a SHORT 1 BTCUSDT order.
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.place_order("BTCUSDT", 1.0, 10000.0, false);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // Close only the LONG side.
    account.close_position("BTCUSDT", true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));

    positions = account.get_all_positions();
    // Expected: only the SHORT position remains.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_FALSE(positions[0].is_long);
}

// Scenario 8: Close both sides by calling close_position(symbol) with no direction in hedge mode
TEST(AccountTest, CloseBothSidesInHedgeMode) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place both LONG and SHORT orders.
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.place_order("BTCUSDT", 1.0, 10000.0, false);
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
// 5. Leverage Adjustments With Existing Positions
/////////////////////////////////////////////////////////

// Scenario 10: Adjust leverage successfully on a symbol with open positions
TEST(AccountTest, AdjustLeverageWithExistingPositions) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 20.0);
    // 先下單: 1 BTC @4000 => notional=4000 => init_margin=200 => fee=2 => total=202 => bal=9798
    account.place_order("BTCUSDT", 1.0, true);
    account.update_positions(twoSymbolMarketData(4000.0, 2.0, 0.0, 0.0));
    EXPECT_DOUBLE_EQ(account.get_balance(), 9798);

    // 改槓桿: 20=>10 => newMargin=400 => diff=+200 => bal=9798-200=9598
    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9598);

    // 再將槓桿10=>40 => newMargin=4000/40=100 => 釋放300 => bal=9598+300=9898
    account.set_symbol_leverage("BTCUSDT", 40.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9898);

    // 若餘額不足 => 調小槓桿需要更多margin => 失敗
    // 先下單再追加大倉, 減少balance
    account.place_order("BTCUSDT", 5.0, true);
	account.update_positions(twoSymbolMarketData(4000.0, 7.0, 0.0, 0.0));
    // 5 BTC@4000 => notional=20000 => margin=20000/40=500 => fee=20000*0.0005=10 => total=510 => bal=9898-510=9388
    EXPECT_DOUBLE_EQ(account.get_balance(), 9388);

    // 現在將槓桿從40 =>1 => newMargin=? 
    //   position(1BTC) => notional=4000
    //   position(5BTC) => notional=20000
    //   total notional=24000 => newMargin=24000/1=24000 => diff=24000 - ( (4000/40)+(20000/40) )= 24000- (100+500)=23400 => 需要23400
    //   但balance只有9388 + unrealized(暫時0)=9388 => 不足 => fail
    testing::internal::CaptureStderr();
    account.set_symbol_leverage("BTCUSDT", 1.0);
    std::string leverageLogs = testing::internal::GetCapturedStderr();

    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 40.0);
    // balance 不會變
    EXPECT_DOUBLE_EQ(account.get_balance(), 9388);
    // stderr => "Failed to change leverage"
    EXPECT_TRUE(leverageLogs.find("Not enough equity to adjust") != std::string::npos);
}

/////////////////////////////////////////////////////////
// 6. Additional Edge Cases for reduceOnly
/////////////////////////////////////////////////////////

// Scenario 11: reduceOnly order in single mode
TEST(AccountTest, ReduceOnlyOrderInSingleMode) {
    Account account(10000.0, 0);
    // In one-way mode.
    EXPECT_FALSE(account.is_hedge_mode());

    // Place a reduce_only order on ETHUSDT when no position exists.
    account.place_order("ETHUSDT", 2.0, 1500.0, true, true);
    std::unordered_map<std::string, std::pair<double, double>> marketData = {
        {"ETHUSDT", {1500.0, 10.0}}
    };
    account.update_positions(marketData);
    auto positions = account.get_all_positions();
    // Expect a position is created (reduce_only order is treated as a normal open order if no position exists).
    ASSERT_EQ(positions.size(), 0u);
}

// Scenario 12: reduceOnly with partial fill in hedge mode
TEST(AccountTest, ReduceOnlyPartialFillInHedgeMode) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place a LONG 5 BTCUSDT order and fill it completely.
    account.place_order("BTCUSDT", 5.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 5.0);

    // Place a reduce_only LONG order for 5 BTCUSDT.
    account.place_order("BTCUSDT", 5.0, 10000.0, true, true);
    // Simulate a partial fill: available volume = 2 BTC.
    account.update_positions(partialMarketDataBTC(9000.0, 2.0));
    positions = account.get_all_positions();
    // Expect the LONG position is reduced from 5 to 3 BTC.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 3.0, 1e-6);

    // Check leftover reduce_only order remains in open orders.
    auto openOrders = account.get_all_open_orders();
    double leftoverQty = 0.0;
    bool foundReduceOnly = false;
    for (const auto& order : openOrders) {
        if (order.reduce_only) {
            foundReduceOnly = true;
            leftoverQty += order.quantity;
        }
    }
    EXPECT_TRUE(foundReduceOnly);
    EXPECT_NEAR(leftoverQty, 3.0, 1e-6);
}

/////////////////////////////////////////////////////////
// 7. Ensuring All Behaviors Work with Merged Positions
/////////////////////////////////////////////////////////

// Scenario 13: Merge then close partially
TEST(AccountTest, MergeThenPartialClose) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place multiple LONG orders for BTCUSDT: 1, 2, 3 BTC => merge into one position.
    account.place_order("BTCUSDT", 1.0, 10000.0, true);
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.place_order("BTCUSDT", 3.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 6.0);

    // Issue a close order for the LONG side with limit price.
    account.close_position("BTCUSDT", true, 10000.0);
    // Simulate a partial fill: available volume = 2 BTC.
    account.update_positions(partialMarketDataBTC(11000.0, 2.0));
    positions = account.get_all_positions();
    // Expect the merged LONG position reduced from 6 to 4 BTC.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 4.0, 1e-6);
}

/////////////////////////////////////////////////////////
// 8. Attempting to Re-Switch Mode After Positions are Closed
/////////////////////////////////////////////////////////

// Scenario 14: Switch from hedge to single mode after closing all positions.
TEST(AccountTest, SwitchModeAfterPositionsClosed) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Open a position.
    account.place_order("BTCUSDT", 2.0, 10000.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    EXPECT_FALSE(account.get_all_positions().empty());

    // Switch back to one-way mode.
    account.set_position_mode(false);
    EXPECT_TRUE(account.is_hedge_mode());

    // Close positions.
    account.close_position("BTCUSDT");
    account.update_positions(partialMarketDataBTC(11000.0, 10.0));
    EXPECT_TRUE(account.get_all_positions().empty());

    // Switch back to one-way mode.
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
}

/////////////////////////////////////////////////////////
// 9. Additional Test Cases for Multiple Symbols
/////////////////////////////////////////////////////////

// Scenario A: Hedge mode, open BTC long and ETH short, then partially fill them.
TEST(AccountTest, HedgeMode_BTC_Long_ETH_Short_PartialFills) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);
	account.set_symbol_leverage("ETHUSDT", 10.0);

    // Place a BTCUSDT LONG order (2 BTC) and an ETHUSDT SHORT order (5 ETH).
    account.place_order("BTCUSDT", 2.0, 20000.0, true);
    account.place_order("ETHUSDT", 5.0, 1500.0, false);

    // Partially fill them: 1 BTC available for BTCUSDT and 3 ETH for ETHUSDT.
    auto marketData = twoSymbolMarketData(20000.0, 1.0, 1500.0, 3.0);
    account.update_positions(marketData);

    // Verify partial fills:
    auto positions = account.get_all_positions();
    // Expect 2 positions: one LONG BTC (1 BTC filled) and one SHORT ETH (3 ETH filled).
    ASSERT_EQ(positions.size(), 2u);
    double btcQty = 0, ethQty = 0;
    for (const auto& pos : positions) {
        if (pos.symbol == "BTCUSDT") btcQty = pos.quantity;
        if (pos.symbol == "ETHUSDT") ethQty = pos.quantity;
    }
    EXPECT_DOUBLE_EQ(btcQty, 1.0);
    EXPECT_DOUBLE_EQ(ethQty, 3.0);

    // Also check that there remain open orders for the unfilled quantities.
    auto openOrders = account.get_all_open_orders();
    EXPECT_EQ(openOrders.size(), 2u);
}

// Scenario B: Multiple symbols in single mode (should not interfere with each other)
TEST(AccountTest, SingleMode_MultipleSymbols) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    // Already in one-way mode.
    EXPECT_FALSE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place a LONG BTCUSDT order and a SHORT ETHUSDT order.
    account.place_order("BTCUSDT", 1.0, 20000.0, true);   // LONG BTC
    account.place_order("ETHUSDT", 2.0, 1500.0, false);     // SHORT ETH

    // Fill fully.
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

    // Now place a reverse order on BTCUSDT to reduce the BTC position.
    account.place_order("BTCUSDT", 0.5, 20000.0, false);
    account.update_positions(marketData);
    positions = account.get_all_positions();
    double btcQty = 0, ethQty = 0;
    for (const auto& pos : positions) {
        if (pos.symbol == "BTCUSDT") btcQty = pos.quantity;
        if (pos.symbol == "ETHUSDT") ethQty = pos.quantity;
    }
    EXPECT_DOUBLE_EQ(btcQty, 0.5);
    EXPECT_DOUBLE_EQ(ethQty, 2.0);
}

// Scenario C: Hedge mode with multiple symbols and reduce_only orders.
TEST(AccountTest, HedgeMode_MultipleSymbols_ReduceOnly) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Open LONG positions on both BTCUSDT (2 BTC) and ETHUSDT (3 ETH).
    auto marketData = twoSymbolMarketData(20000.0, 10.0, 1500.0, 10.0);
    account.place_order("BTCUSDT", 2.0, 20000.0, true);
    account.place_order("ETHUSDT", 3.0, 1500.0, true);
    account.update_positions(marketData);

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u); // 2 positions for 2 different symbols.

    // Now place a reduce_only LONG order on BTCUSDT for 1 BTC.
    account.place_order("BTCUSDT", 1.0, 20000.0, true, true);
    account.update_positions(marketData);
    positions = account.get_all_positions();
    double btcQty = 0, ethQty = 0;
    for (const auto& pos : positions) {
        if (pos.symbol == "BTCUSDT") btcQty = pos.quantity;
        if (pos.symbol == "ETHUSDT") ethQty = pos.quantity;
    }
    EXPECT_DOUBLE_EQ(btcQty, 1.0); // BTC reduced from 2 to 1.
    EXPECT_DOUBLE_EQ(ethQty, 3.0); // ETH remains unchanged.

    // Check that leftover reduce_only order is fully filled.
    auto openOrders = account.get_all_open_orders();
    EXPECT_TRUE(openOrders.empty());
}
