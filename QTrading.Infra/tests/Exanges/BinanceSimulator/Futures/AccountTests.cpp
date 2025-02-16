#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream>   // for CaptureStdout/CaptureStderr

// Include your updated Account, Config, MarketData headers
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/DataProvider/MarketData.hpp"

//-------------------------------------------------------------------
// Google Test Fixture
//-------------------------------------------------------------------
class AccountTest : public ::testing::Test {
protected:
    void SetUp() override {
        // runs before each TEST_F
    }
    void TearDown() override {
        // runs after each TEST_F
    }
};

//-------------------------------------------------------------------
// 1) Test: Constructor, get_balance, total_unrealized_pnl, get_equity
//-------------------------------------------------------------------
TEST_F(AccountTest, ConstructorAndGetters) {
    Account account(1000.0, 0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 1000.0);
}

//-------------------------------------------------------------------
// 2) Test: set_symbol_leverage / get_symbol_leverage
//    - normal set
//    - set <=0 => throw runtime_error
//-------------------------------------------------------------------
TEST_F(AccountTest, SetAndGetSymbolLeverage) {
    Account account(2000.0, 0);
    // default not set => 1
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 1.0);

    // set 50x
    account.set_symbol_leverage("BTCUSDT", 50.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 50.0);

    // leverage <=0 => runtime_error
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", 0.0), std::runtime_error);
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", -10.0), std::runtime_error);
}

//-------------------------------------------------------------------
// 3) Test: place_order - success
//    - check margin+fee deduction
//    - capture stdout
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderSuccess) {
    // VIP=0 => maker fee=0.00020, taker fee=0.00050
    Account account(10000.0, 0);

    testing::internal::CaptureStdout();

    // place order: BTCUSDT, 1 BTC @7000, long, limit
    //   default leverage=1
    //   notional=7000 => init_margin=7000 => fee=7000*0.00020=1.4 => total=7001.4
    //   balance=10000-7001.4=2998.6
    account.place_order("BTCUSDT", 1.0, 7000.0, true, false);

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_DOUBLE_EQ(account.get_balance(), 2998.6);
    EXPECT_TRUE(output.find("BTCUSDT") != std::string::npos);
    EXPECT_TRUE(output.find("LONG") != std::string::npos);
}

//-------------------------------------------------------------------
// 4) Test: place_order - fail => insufficient equity
//    - balance unchanged
//    - stderr => "Not enough equity"
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderInsufficientEquity) {
    // VIP=0 => taker fee=0.00050
    Account account(100.0, 0); // very little balance

    testing::internal::CaptureStderr();

    // e.g. 5 BTC @1000 => notional=5000 => leverage=1 => margin=5000 => fee=5000*0.00050=2.5 => total=5002.5 => >100 => fail
    account.place_order("BTCUSDT", 5.0, 1000.0, true, true);
    std::string errOutput = testing::internal::GetCapturedStderr();

    EXPECT_DOUBLE_EQ(account.get_balance(), 100.0);
    EXPECT_TRUE(errOutput.find("Not enough equity") != std::string::npos);
}

//-------------------------------------------------------------------
// 5) Test: place_order - fail => exceed tier's max leverage
//    - balance unchanged
//    - stderr => "x > tier max="
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderExceedTierLeverage) {
    Account account(100000.0, 0);

    // set symbol leverage=60 => if notional triggers a tier that only allows 50 => fail
    account.set_symbol_leverage("BTCUSDT", 60.0);

    testing::internal::CaptureStderr();
    account.place_order("BTCUSDT", 120.0, 100000.0, true, false);
    std::string out = testing::internal::GetCapturedStderr();

    EXPECT_DOUBLE_EQ(account.get_balance(), 100000.0);
    EXPECT_TRUE(out.find("x > tier max=") != std::string::npos);
}

//-------------------------------------------------------------------
// 6) Test: update_positions (multiple symbols, different directions)
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsMultipleSymbols) {
    // VIP=1 => maker=0.00016, taker=0.00040 (from config)
    Account account(50000.0, 1);

    account.set_symbol_leverage("BTCUSDT", 20.0);
    // order1: BTCUSDT, long, limit => 2 BTC @20000 => ...
    //   notional=40000 => init_margin=2000 => makerFee=40000*0.00016=6.4 => total=2006.4 => bal=47993.6
    account.place_order("BTCUSDT", 2.0, 20000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 47993.6);

    account.set_symbol_leverage("ETHUSDT", 20.0);
    // order2: ETHUSDT, short, market => 10 *2000 => notional=20000 => init_margin=1000 => takerFee=8 => total=1008 => bal=46985.6
    account.place_order("ETHUSDT", 10.0, 2000.0, false, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 46985.6);

    // feed prices
    std::map<std::string, double> prices{
        {"BTCUSDT", 21000.0}, // up => +1000 per BTC => +2000
        {"ETHUSDT", 1900.0}   // down => short gain => +100
        // per ETH => *10= +1000
    };
    account.update_positions(prices);

    // total_unrealized=3000
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 3000.0);
    // equity=46985.6+3000=49985.6
    EXPECT_DOUBLE_EQ(account.get_equity(), 49985.6);
}

//-------------------------------------------------------------------
// 7) Test: update_positions - missing symbol => skip
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsMissingSymbolPrice) {
    Account account(30000.0, 0);

    account.set_symbol_leverage("BTCUSDT", 20.0);
    // order1: BTCUSDT long, market => notional=20000 => ...
    //   margin=1000 => fee=10 => total=1010 => bal=28990
    account.place_order("BTCUSDT", 1.0, 20000.0, true, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 28990.0);

    account.set_symbol_leverage("ETHUSDT", 20.0);
    // order2: ETHUSDT short, limit => 5 *1500 => ...
    //   margin=375 => fee=1.5 => total=376.5 => bal=28613.5
    account.place_order("ETHUSDT", 5.0, 1500.0, false, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 28613.5);

    // only provide BTC price
    std::map<std::string, double> priceMap{
        {"BTCUSDT", 21000.0}
    };

    testing::internal::CaptureStderr();
    account.update_positions(priceMap);
    std::string logs = testing::internal::GetCapturedStderr();

    // BTC => +1000
    // ETH => skip => 0
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1000.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 29613.5);
    EXPECT_TRUE(logs.find("Missing price for ETHUSDT") != std::string::npos);
}

//-------------------------------------------------------------------
// 8) Test: update_positions - liquidation triggered
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsTriggerLiquidation) {
    Account account(4000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 20.0);

    // place => 5 BTC@1000 => notional=5000 => margin=250 => fee=1 => total=251 => bal=3749
    account.place_order("BTCUSDT", 5.0, 1000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 3749.0);

    // price => 100 => big loss => liquidation
    std::map<std::string, double> crashPrice{
        {"BTCUSDT", 100.0}
    };

    testing::internal::CaptureStderr();
    account.update_positions(crashPrice);
    std::string out = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(out.find("Liquidation triggered") != std::string::npos);
    EXPECT_DOUBLE_EQ(account.get_balance(), 0.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 0.0);
}

//-------------------------------------------------------------------
// 9) Test: close_position (legacy by symbol)
//-------------------------------------------------------------------
TEST_F(AccountTest, ClosePosition) {
    Account account(10000.0, 0);

    account.set_symbol_leverage("BTCUSDT", 20.0);

    // 1 BTC@5000 => margin=250 => fee=1 => total=251 => bal=9749
    account.place_order("BTCUSDT", 1.0, 5000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9749.0);

    // update => price=6000 => unrealized= +1000
    std::map<std::string, double> pMap{
        {"BTCUSDT", 6000.0}
    };
    account.update_positions(pMap);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1000.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 10749.0);

    // close => realized +1000 => closeFee=6000*0.00020=1.2 => return=250+1000-1.2=1248.8 => bal=9749+1248.8=10997.8
    testing::internal::CaptureStdout();
    account.close_position("BTCUSDT", 6000.0, false);
    std::string msg = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(msg.find("realizedPnL=1000") != std::string::npos);
    EXPECT_NEAR(account.get_balance(), 10997.8, 1e-9);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 10997.8);

    // close again => "No position found"
    testing::internal::CaptureStderr();
    account.close_position("BTCUSDT", 6000.0, false);
    std::string msg2 = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(msg2.find("No position found for symbol BTCUSDT") != std::string::npos);
}

//-------------------------------------------------------------------
// 10) Test: adjust_position_leverage
//-------------------------------------------------------------------
TEST_F(AccountTest, AdjustPositionLeverage) {
    Account account(10000.0, 0);

    account.set_symbol_leverage("BTCUSDT", 20.0);
    // 1 BTC@4000 => margin=200 => takerFee=2 => total=202 => bal=9798
    account.place_order("BTCUSDT", 1.0, 4000.0, true, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9798.0);

    // 20=>10 => need +200 => bal=9598
    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9598.0);

    // 10=>40 => release margin => bal=9898
    account.set_symbol_leverage("BTCUSDT", 40.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9898.0);

    // add bigger order => 5 BTC => margin=20000/40=500 => fee=20000*0.00050=10 => total=510 => bal=9388
    account.place_order("BTCUSDT", 5.0, 4000.0, true, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9388.0);

    // try 40=>1 => huge margin => fail
    testing::internal::CaptureStderr();
    account.set_symbol_leverage("BTCUSDT", 1.0);
    std::string leverageLogs = testing::internal::GetCapturedStderr();

    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 40.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9388.0);
    EXPECT_TRUE(leverageLogs.find("Not enough equity to adjust") != std::string::npos);
}

//-------------------------------------------------------------------
// 11) Test: HedgeMode - simultaneously hold LONG and SHORT for same symbol
//     (Does NOT offset automatically)
//-------------------------------------------------------------------
TEST_F(AccountTest, HedgeMode_LongAndShortSameSymbol) {
    Account account(100000.0, 0);

    // set 10x
    account.set_symbol_leverage("BTCUSDT", 10.0);

    // place a LONG limit order: 2 BTC
    account.place_order("BTCUSDT", 2.0, 30000.0, true, false);
    // place a SHORT limit order: 1 BTC
    account.place_order("BTCUSDT", 1.0, 30000.0, false, false);

    // assume price=30000, large volume => both fill
    std::map<std::string, std::pair<double, double>> feed;
    feed["BTCUSDT"] = std::make_pair(30000.0, 999999.0);
    account.update_positions({
        {"BTCUSDT", {30000.0, 999999.0}}
        });

    // we expect 2 positions: one long(2 BTC), one short(1 BTC)
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    bool foundLong = false;
    bool foundShort = false;
    for (auto& p : positions) {
        if (p.symbol == "BTCUSDT" && p.is_long && p.quantity == 2.0) {
            foundLong = true;
        }
        if (p.symbol == "BTCUSDT" && !p.is_long && p.quantity == 1.0) {
            foundShort = true;
        }
    }
    EXPECT_TRUE(foundLong);
    EXPECT_TRUE(foundShort);

    // next update => price=31000 => 
    //   long(2 BTC) => + (31000-30000)*2= +2000
    //   short(1 BTC) => (30000-31000)*1= -1000 => net +1000
    account.update_positions({
        {"BTCUSDT", {31000.0, 999999.0}}
        });
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1000.0);
}

//-------------------------------------------------------------------
// 12) Test: Partial Fill - leftover remains in open_orders
//-------------------------------------------------------------------
TEST_F(AccountTest, PartialFillRemainsInOpenOrders) {
    Account account(5000.0, 0);

    // 1) Place an order with quantity=2
    //    e.g. 2 BTC @1000 => notional=2000 => margin=2000 => fee=2000*0.00020=0.4 => total=2000.4 => bal=2999.6
    account.place_order("BTCUSDT", 2.0, 1000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 2999.6);

    // 2) The market has volume=1 => partial fill => leftover=1
    //    fill_qty=1 => notional=1000 => margin=1000 => fee=0.2 => total=1000.2
    //    deduct => bal=2999.6 -1000.2=1999.4
    // leftover=1 => remains
    std::map<std::string, std::pair<double, double>> partialFeed{
        {"BTCUSDT", {1000.0, 1.0}} // volume=1 => partial fill
    };
    account.update_positions(partialFeed);

    auto openOrders = account.get_all_open_orders();
    ASSERT_EQ(openOrders.size(), 1u); // leftover
    EXPECT_DOUBLE_EQ(openOrders[0].quantity, 1.0);

    // check new balance
    EXPECT_DOUBLE_EQ(account.get_balance(), 1999.4);

    // 3) Next time => bigger volume => fill leftover=1
    //    margin=1000 => fee=0.2 => total=1000.2 => bal=999.2
    //    no leftover
    std::map<std::string, std::pair<double, double>> nextFeed{
        {"BTCUSDT", {1000.0, 10.0}}
    };
    account.update_positions(nextFeed);

    // now no open orders
    EXPECT_TRUE(account.get_all_open_orders().empty());
    EXPECT_DOUBLE_EQ(account.get_balance(), 999.2);
}

//-------------------------------------------------------------------
// 13) Test: cancel_order_by_id
//     - partial fill, leftover => then cancel leftover => check open_orders
//-------------------------------------------------------------------
TEST_F(AccountTest, CancelOrderById) {
    Account account(5000.0, 0);

    // 1) place order => quantity=3 => e.g. 3 BTC@1000 => margin=3000 => fee=0.6 => total=3000.6 => bal=1999.4
    //    (since VIP=0 => makerFee=0.00020)
    account.place_order("BTCUSDT", 3.0, 1000.0, true, false);
    auto ords = account.get_all_open_orders();
    ASSERT_EQ(ords.size(), 1u);
    int theOrderId = ords[0].id; // let's store ID

    EXPECT_DOUBLE_EQ(account.get_balance(), 1999.4);

    // 2) update => partial fill => fill_qty=1 => leftover=2
    //    => new bal=999.2
    account.update_positions({
        {"BTCUSDT", {1000.0, 1.0}}
        });

    // leftover=2 => open_orders still 1
    ords = account.get_all_open_orders();
    ASSERT_EQ(ords.size(), 1u);
    EXPECT_DOUBLE_EQ(ords[0].quantity, 2.0);
    EXPECT_EQ(ords[0].id, theOrderId);

    EXPECT_DOUBLE_EQ(account.get_balance(), 999.2);

    // 3) Cancel leftover => open_orders => 0
    account.cancel_order_by_id(theOrderId);
    ords = account.get_all_open_orders();
    EXPECT_EQ(ords.size(), 0u);
}

//-------------------------------------------------------------------
// 14) Test: close_position_by_id
//     - create multiple positions, close one by ID => verify only that one is closed
//-------------------------------------------------------------------
TEST_F(AccountTest, ClosePositionById) {
    Account account(10000.0, 0);

    account.set_symbol_leverage("BTCUSDT", 10.0);

    // 1) place two separate limit orders => one LONG(2 BTC@3000), one LONG(3 BTC@3000).
    //    In HedgeMode, if we treat "same direction => combine"? 
    //    We might want them to be separate by artificially adjusting code 
    //    or we do different symbols. For demonstration, let's do different symbols.
    account.place_order("BTCUSDT", 2.0, 3000.0, true, false);  // notional=6000 => margin=600 => fee=1.2 => total=601.2 => bal=9398.8
    account.place_order("ETHUSDT", 5.0, 2000.0, true, false);  // notional=10000 => margin=1000 => fee=2 => total=1002 => bal=8396.8

    // fill them by providing enough volume
    account.update_positions({
        {"BTCUSDT", {3000.0, 999999.0}},
        {"ETHUSDT", {2000.0, 999999.0}}
        });
    auto positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);

    // let's record their IDs
    int pid1 = positions[0].id;
    int pid2 = positions[1].id;

    // 2) let's close the second position by ID => no price move => realized=0 => fee ~ ?
    //    if we used limit => makerFee=0.00020 => close_notional= (2000*5)=10000 => fee=2 => 
    //    returned_amount= initial_margin(1000)+ realized(0)-2=998 => bal=8396.8+998= 9394.8
    testing::internal::CaptureStdout();
    account.close_position_by_id(pid2, 2000.0, false);
    std::string out = testing::internal::GetCapturedStdout();

    // check remain only 1 position
    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    // the only position left should be pid1
    EXPECT_EQ(positions[0].id, pid1);

    // check new balance 
    // original=8396.8 => + (1000 -2)=998 => 9394.8
    EXPECT_DOUBLE_EQ(account.get_balance(), 9394.8);

    // 3) try close_position_by_id an invalid ID => "No position found"
    testing::internal::CaptureStderr();
    account.close_position_by_id(pid2, 2000.0, false);
    std::string errLogs = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(errLogs.find("No position with ID=") != std::string::npos);
}
