#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream> // for CaptureStdout, CaptureStderr
#include "Exanges/BinanceSimulator/Futures/Account.hpp"

// A Google Test fixture class
class AccountTest : public ::testing::Test {
protected:
    void SetUp() override {
        // This method is called before each TEST_F
    }

    void TearDown() override {
        // This method is called after each TEST_F
    }
};

/**
 * 1) Constructor & Basic Getter
 */
TEST_F(AccountTest, ConstructorAndGetters) {
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
TEST_F(AccountTest, SetAndGetSymbolLeverage) {
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
TEST_F(AccountTest, PlaceOrderSuccessCheckOpenOrders) {
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
TEST_F(AccountTest, UpdatePositionsPartialFillSameOrder) {
    Account account(5000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place a limit order for 5 BTC at price=1000
    account.place_order("BTCUSDT", 5.0, 1000.0, true);

    // build market data => current_price=1000, available_volume=2 => partial fill
    std::unordered_map<std::string, std::pair<double, double>> marketData{
        {"BTCUSDT",{1000.0, 2.0}}
    };

    // update_positions => expect partial fill of 2 BTC
    account.update_positions(marketData);

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
    std::unordered_map<std::string, std::pair<double, double>> nextData{
        {"BTCUSDT",{1000.0, 10.0}}
    };
    account.update_positions(nextData);

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
TEST_F(AccountTest, ClosePositionBySymbol) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // 1) Place a limit order => open a position
    account.place_order("BTCUSDT", 2.0, 1000.0, true);

    // 2) match => fully filled
    std::unordered_map<std::string, std::pair<double, double>> data1{
        {"BTCUSDT",{1000.0, 5.0}}
    };
    account.update_positions(data1);
    // now we have a position of 2 BTC at 1000

    // Let the price go up => e.g., 1200 => unrealized profit
    std::unordered_map<std::string, std::pair<double, double>> data2{
        {"BTCUSDT",{1200.0, 5.0}}
    };
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
 * 6) close_position_by_id => only close that specific position
 *    - if user has multiple positions on the same symbol
 */
TEST_F(AccountTest, ClosePositionByID) {
    Account account(20000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place two separate orders => leads to two separate positions
    // each order => partial fill to show we can have distinct positions
    account.place_order("BTCUSDT", 3.0, 5000.0, true);
    account.place_order("BTCUSDT", 2.0, 5000.0, true);

    // update => fill them
    std::unordered_map<std::string, std::pair<double, double>> data{
        {"BTCUSDT",{5000.0, 10.0}}
    };
    account.update_positions(data);

    // We should have 2 distinct positions
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    int posID1 = positions[0].id;
    int posID2 = positions[1].id;

    // close_position_by_id(posID1, price=5100 => limit close)
    account.close_position_by_id(posID1, 5100.0);

    // There's a new "closing order" => update to fill it
    std::unordered_map<std::string, std::pair<double, double>> data2{
        {"BTCUSDT",{5100.0, 10.0}}
    };
    account.update_positions(data2);

    // Now posID1 is closed => only posID2 left
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].id, posID2);
}

/**
 * 7) cancel_order_by_id => remove leftover portion
 *    - If partial fill already done, only leftover is removed
 */
TEST_F(AccountTest, CancelOrderByID) {
    Account account(5000.0, 0);
    // place a large order
    account.place_order("BTCUSDT", 5.0, 500.0, true);
    auto orders = account.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    int oid = orders[0].id;

    // partial fill => 2 BTC
    std::unordered_map<std::string, std::pair<double, double>> data1{
        {"BTCUSDT",{500.0, 2.0}}
    };
    account.update_positions(data1);

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
 * 8) Liquidation test:
 *    - Large position => negative equity => liquidation triggers
 *    - verify positions cleared, balance=0
 */
TEST_F(AccountTest, Liquidation) {
    Account account(2000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place an order => e.g., 4 BTC at 500 => notional=2000 => margin=200 => fee=some small => ok
    account.place_order("BTCUSDT", 4.0, 500.0, true);

    // fill it
    std::unordered_map<std::string, std::pair<double, double>> fillData{
        {"BTCUSDT",{500.0, 10.0}}
    };
    account.update_positions(fillData);
    EXPECT_DOUBLE_EQ(account.get_all_positions().size(), 1);

    // now let's crash price => 50 => negative PnL => triggers liquidation
    std::unordered_map<std::string, std::pair<double, double>> crashData{
        {"BTCUSDT",{50.0, 10.0}}
    };

    testing::internal::CaptureStderr();
    account.update_positions(crashData);
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);
    // positions_ cleared
    EXPECT_TRUE(account.get_all_positions().empty());
    // balance=0
    EXPECT_DOUBLE_EQ(account.get_balance(), 0.0);
}

/**
 * 9) Hedge-mode: same symbol, different direction => separate positions
 *    - place two orders (one long, one short) => check both get filled => no offset
 */
TEST_F(AccountTest, HedgeModeSameSymbolOppositeDirection) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // 1) LONG order
    account.place_order("BTCUSDT", 2.0, 3000.0, true);
    // 2) SHORT order
    account.place_order("BTCUSDT", 1.0, 3000.0, false);

    // fill them all
    std::unordered_map<std::string, std::pair<double, double>> data{
        {"BTCUSDT",{3000.0, 10.0}}
    };
    account.update_positions(data);

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

/**
 * 10) Different orders => different positions (even if same symbol, same direction)
 *    - confirm "Distinguishes positions from different orders"
 */
TEST_F(AccountTest, SameSymbolSameDirectionDifferentOrders) {
    Account account(10000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place two separate orders => even though both are LONG BTC, 
    // each one should open a separate position
    account.place_order("BTCUSDT", 1.0, 3000.0, true); // order A
    account.place_order("BTCUSDT", 2.0, 3000.0, true); // order B

    std::unordered_map<std::string, std::pair<double, double>> data{
        {"BTCUSDT",{3000.0, 10.0}}
    };
    account.update_positions(data);

    // expect 2 distinct positions
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // check total quantity = 3, but in 2 separate positions
    double totalQty = 0.0;
    for (auto& p : positions) {
        totalQty += p.quantity;
    }
    EXPECT_DOUBLE_EQ(totalQty, 3.0);

    // each position has its own position_id, order_id
    EXPECT_NE(positions[0].id, positions[1].id);
    EXPECT_NE(positions[0].order_id, positions[1].order_id);
}

//
// End of tests
//
