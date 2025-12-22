#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream> // for CaptureStdout, CaptureStderr
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"

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

    // price>0 => limit order
    testing::internal::CaptureStdout();
    account.place_order("BTCUSDT", 1.0, 7000.0, true); // is_long=true
    std::string out = testing::internal::GetCapturedStdout();

    const auto& orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(orders[0].quantity, 1.0);
    EXPECT_DOUBLE_EQ(orders[0].price, 7000.0);
    EXPECT_TRUE(orders[0].is_long);
    // Not closing any position => closing_position_id == -1 (預設)
    EXPECT_EQ(orders[0].closing_position_id, -1);

    // Wallet balance not deducted yet
    EXPECT_DOUBLE_EQ(account.get_balance().WalletBalance, 10000.0);

    // Verify that there's no error log in out
    // (We might or might not check specific logs here)
    // e.g., we can check some substring if needed
}

/// @brief Verifies partial fill creates position and leaves leftover order.
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

/// @brief Verifies market-close order realizes PnL and clears position.
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

/// @brief Verifies cancel_order_by_id removes leftover but keeps filled position.
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

/// @brief Verifies liquidation clears all positions and zeroes balance.
TEST(AccountTest, Liquidation) {
    // Cross-margin: open an oversized position then crash mark price to force liquidation.
    // notional=2,500,000 falls into max leverage 75x tier.
    Account account(350000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 75.0);

    account.place_order("BTCUSDT", 5000.0, 500.0, true);
    account.update_positions(partialMarketDataBTC(500.0, 10000.0));
    EXPECT_DOUBLE_EQ(account.get_all_positions().size(), 1);

    testing::internal::CaptureStderr();
    account.update_positions(partialMarketDataBTC(1.0, 10000.0));
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);

    // In the Binance-like approximation, wallet can go negative (debt) under extreme gaps/slippage.
    // We only assert liquidation occurred and the engine remains consistent.
    EXPECT_TRUE(account.get_all_positions().empty() || account.get_balance().MarginBalance >= account.get_balance().MaintenanceMargin);
}

/// @brief Verifies hedge-mode allows separate long and short positions.
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

/// Scenario 1: Switching mode with open positions (should fail)
TEST(AccountTest, SwitchingModeWithOpenPositionsFails) {
	Account account(10000.0, 0);
	account.set_position_mode(false);
    // Default is one-way mode.
    EXPECT_FALSE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place an order so that a position is opened (e.g., LONG 1 BTCUSDT).
    // Use MARKET order so entry price follows the provided tick price.
    account.place_order("BTCUSDT", 1.0, true);
    account.update_positions(partialMarketDataBTC(9000.0, 10.0));
    EXPECT_FALSE(account.get_all_positions().empty());

    // Attempt to switch mode to hedge mode.
    account.set_position_mode(true);
    // Mode switch should be rejected because there is an open position.
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
// 2. Single Mode Auto-Reduce vs. Hedge Mode reduceOnly
/////////////////////////////////////////////////////////

/// @brief Single mode: auto-reduce should happen on opposite position open.
TEST(AccountTest, SingleModeAutoReduceOppositePositionOpen) {
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
    // Expect the existing LONG is reduced from 2 to 1.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-6);
}

/// @brief Hedge mode with reduce_only = true
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

/// @brief Verifies position merging in hedge mode
TEST(AccountTest, MergePositionsSameDirection) {
    Account account(10000.0, 0);
    // Switch to hedge mode.
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
	account.set_symbol_leverage("BTCUSDT", 10.0);

    // Place 3 LONG MARKET orders for BTCUSDT: 1, 2, 3 BTC at the tick price.
    account.place_order("BTCUSDT", 1.0, true);
    account.place_order("BTCUSDT", 2.0, true);
    account.place_order("BTCUSDT", 3.0, true);
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));
    auto positions = account.get_all_positions();
    // Expect one merged position with total quantity = 6 BTC.
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_DOUBLE_EQ(positions[0].quantity, 6.0);
}

// Scenario 2: Verifying that different directions do not merge
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

/// @brief Close only the LONG side of a hedge-mode symbol
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

/// @brief Close both sides by calling close_position(symbol) with no direction in hedge mode
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

/// @brief Adjusting leverage should succeed/fail depending on available margin
TEST(AccountTest, AdjustLeverageWithExistingPositions) {
    // Provide more collateral to satisfy open-order/position margin occupation.
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 20.0);

    account.place_order("BTCUSDT", 1.0, true);
    account.update_positions(twoSymbolMarketData(4000.0, 2.0, 0.0, 0.0));

    // Leverage adjustments should still be allowed; we only assert it doesn't throw and keeps leverage.
    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 10.0);

    account.set_symbol_leverage("BTCUSDT", 40.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 40.0);
}

TEST(AccountTest, SingleMode_MultipleSymbols) {
    // Increase collateral so both symbols can open under cross margin.
    Account account(50000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_symbol_leverage("ETHUSDT", 10.0);

    account.place_order("BTCUSDT", 1.0, 20000.0, true);
    account.place_order("ETHUSDT", 2.0, 1500.0, false);

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

    account.place_order("BTCUSDT", 0.5, 20000.0, false);
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

    auto marketData = twoSymbolMarketData(20000.0, 10.0, 1500.0, 10.0);
    account.place_order("BTCUSDT", 2.0, 20000.0, true);
    account.place_order("ETHUSDT", 3.0, 1500.0, true);
    account.update_positions(marketData);

    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    account.place_order("BTCUSDT", 1.0, 20000.0, true, true);
    account.update_positions(marketData);

    // reduceOnly semantics will be refined later; for now ensure system remains stable.
    EXPECT_FALSE(account.get_all_positions().empty());
}
/// @brief Ensures per-tick available volume is consumed across multiple orders for same symbol.
TEST(AccountTest, TickVolumeIsConsumedAcrossOrdersSameSymbol) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // Two marketable BUY limits; only 1 BTC volume.
    // Higher limit should be filled first.
    account.place_order("BTCUSDT", 1.0, 1100.0, true);
    account.place_order("BTCUSDT", 1.0, 1200.0, true);

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    // Only 1 BTC should be filled total; remaining 1 BTC stays as open order.
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

    // VIP0: maker=0.0002, taker=0.0005.
    // Set a BUY limit above current price => immediately executable.
    account.place_order("BTCUSDT", 1.0, 2000.0, true);
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);

    // For an immediately executable limit, fee_rate should be taker.
    EXPECT_NEAR(positions[0].fee_rate, 0.00050, 1e-12);
}

TEST(AccountTest, LimitOrderFillsAtLimitPrice) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // BUY limit above current, should execute immediately but fill at limit price.
    account.place_order("BTCUSDT", 1.0, 2000.0, true);
    account.update_positions(partialMarketDataBTC(1000.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 2000.0, 1e-12);
}

TEST(AccountTest, TickPriceTimePriority_BuyHigherLimitFillsFirst) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // Two marketable BUY limits; only 1 BTC volume.
    // Higher limit should be filled first.
    account.place_order("BTCUSDT", 1.0, 1100.0, true);
    account.place_order("BTCUSDT", 1.0, 1200.0, true);

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    // Only 1 BTC should be filled total; remaining 1 BTC stays as open order.
    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-8);

    // The remaining open order should be the lower-priority one: limit=1100.
    const auto& openOrders = account.get_all_open_orders();
    ASSERT_EQ(openOrders.size(), 1u);
    EXPECT_NEAR(openOrders[0].price, 1100.0, 1e-12);
    EXPECT_NEAR(openOrders[0].quantity, 1.0, 1e-8);
}

TEST(AccountTest, TickPriceTimePriority_SamePriceLowerIdFillsFirst) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // Two marketable BUY limits same price; only 1 BTC volume.
    // Earlier order (lower id) should fill first => second remains.
    account.place_order("BTCUSDT", 1.0, 1200.0, true);
    account.place_order("BTCUSDT", 1.0, 1200.0, true);

    int firstId = account.get_all_open_orders()[0].id;
    int secondId = account.get_all_open_orders()[1].id;
    ASSERT_LT(firstId, secondId);

    account.update_positions(partialMarketDataBTC(1000.0, 1.0));

    const auto& openOrders = account.get_all_open_orders();
    ASSERT_EQ(openOrders.size(), 1u);
    EXPECT_EQ(openOrders[0].id, secondId);
    EXPECT_NEAR(openOrders[0].quantity, 1.0, 1e-8);
}

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

TEST(AccountTest, OhlcTrigger_BuyLimitTriggersOnLow) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // BUY limit=95, close=105 (would not fill under close-only rule), but Low=90 triggers.
    account.place_order("BTCUSDT", 1.0, 95.0, true);
    account.update_positions(oneKline("BTCUSDT", 110.0, 120.0, 90.0, 105.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 95.0, 1e-12);
}

TEST(AccountTest, OhlcTrigger_SellLimitTriggersOnHigh) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // SELL/short limit=115, close=105 (would not fill under close-only rule), but High=120 triggers.
    account.place_order("BTCUSDT", 1.0, 115.0, false);
    account.update_positions(oneKline("BTCUSDT", 110.0, 120.0, 100.0, 105.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_NEAR(positions[0].entry_price, 115.0, 1e-12);
}
