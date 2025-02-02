#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <iostream>   // for CaptureStdout/CaptureStderr

// 包含您的 Account, Config, MarketData 標頭
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/DataProvider/MarketData.hpp"

//-------------------------------------------------------------------
// 測試用小工具: 建立一個簡單的 kline 物件 (只設定ClosePrice即可)
// (在舊版 update_positions(Kline) 會用到, 現在多數測試將用 map)
//-------------------------------------------------------------------
static KlineDto make_kline(double close_price) {
    KlineDto kl;
    kl.ClosePrice = close_price;
    return kl;
}

//-------------------------------------------------------------------
// Google Test Fixture
//-------------------------------------------------------------------
class AccountTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每個 TEST_F 測試開始前執行
    }

    void TearDown() override {
        // 每個 TEST_F 測試結束後執行
    }
};

//-------------------------------------------------------------------
// 1) 測試: Constructor, get_balance, total_unrealized_pnl, get_equity
//-------------------------------------------------------------------
TEST_F(AccountTest, ConstructorAndGetters) {
    Account account(1000.0, 0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 1000.0);
}

//-------------------------------------------------------------------
// 2) 測試: set_symbol_leverage / get_symbol_leverage
//    - 正常設定
//    - 設定 <=0 => 拋出例外
//-------------------------------------------------------------------
TEST_F(AccountTest, SetAndGetSymbolLeverage) {
    Account account(2000.0, 0);
    // 預設沒設定 => -1
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), -1.0);

    // 設置 50x
    account.set_symbol_leverage("BTCUSDT", 50.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 50.0);

    // 槓桿 <=0 => runtime_error
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", 0.0), std::runtime_error);
    EXPECT_THROW(account.set_symbol_leverage("BTCUSDT", -10.0), std::runtime_error);
}

//-------------------------------------------------------------------
// 3) 測試: place_order - 成功案例
//    - 檢查扣除保證金 + 手續費後的 balance
//    - 捕捉 stdout 看日誌
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderSuccess) {
    // VIP=0 => maker fee=0.0002, taker fee=0.0004
    Account account(10000.0, 0);

    // 捕捉 stdout
    testing::internal::CaptureStdout();

    // 下單: BTCUSDT, 1 BTC @7000, 多單, limit order
    //   未設定槓桿 => 預設=20
    //   notional=7000 => init_margin=350 => fee=7000*0.0002=1.4 => total=351.4
    //   balance=10000-351.4=9648.6
    account.place_order("BTCUSDT", 1.0, 7000.0, true, false);

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_DOUBLE_EQ(account.get_balance(), 9648.6);
    EXPECT_TRUE(output.find("BTCUSDT LONG 1 @ 7000") != std::string::npos);
}

//-------------------------------------------------------------------
// 4) 測試: place_order - 失敗案例 => 權益不足
//    - balance 不變
//    - stderr出現 "Insufficient equity"
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderInsufficientEquity) {
    Account account(100.0, 0); // 餘額極少

    testing::internal::CaptureStderr();
    // 1 BTC @1000 => notional=1000 => 20x => margin=50 => takerFee=1000*0.0004=0.4 => total=50.4
    //   balance=100 => 50.4 <= 100 => 其實夠...? => 我們讓這更大 => 5 BTC
    //   notional=5000 => margin=250 => fee=2 => total=252 => >100 => 不足
    account.place_order("BTCUSDT", 5.0, 1000.0, true, true);
    std::string errOutput = testing::internal::GetCapturedStderr();

    // balance 不變
    EXPECT_DOUBLE_EQ(account.get_balance(), 100.0);
    // 驗證輸出
    EXPECT_TRUE(errOutput.find("Insufficient equity") != std::string::npos);
}

//-------------------------------------------------------------------
// 5) 測試: place_order - 失敗案例 => 超過對應 tier 的 max leverage
//    - balance 不變
//    - stderr出現 "max allowed leverage is X"
//-------------------------------------------------------------------
TEST_F(AccountTest, PlaceOrderExceedTierLeverage) {
    // 餘額夠大，不要卡在餘額不足
    Account account(100000.0, 0);

    // 假設 notional=1*100000=100000 => 依照 margin_tiers:
    //   <=250000 => maxLeverage=50
    // 如果設 symbol_leverage=60 => place_order => 預計拒絕
    account.set_symbol_leverage("BTCUSDT", 60.0);

    testing::internal::CaptureStderr();
    account.place_order("BTCUSDT", 1.0, 100000.0, true, false);
    std::string out = testing::internal::GetCapturedStderr();

    EXPECT_DOUBLE_EQ(account.get_balance(), 100000.0);
    EXPECT_TRUE(out.find("max allowed leverage is 50") != std::string::npos);
}

//-------------------------------------------------------------------
// 6) 測試: update_positions( map<symbol, price> )
//    - 同時持多倉 & 空倉的多個 symbol
//    - 分別對應不同價格
//    - 驗證 total_unrealized_pnl, equity
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsMultipleSymbols) {
    // VIP=1 => maker=0.00018, taker=0.00036
    Account account(50000.0, 1);

    // 下單1: BTCUSDT 多單(限價)
    //   notional= 2*20000=40000 => leverage=20 => init_margin=2000 => fee=40000*0.00018=7.2 => total=2007.2
    //   balance=47992.8
    account.place_order("BTCUSDT", 2.0, 20000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 47992.8);

    // 下單2: ETHUSDT 空單(市價)
    //   notional=10*2000=20000 => leverage=20 => init_margin=1000 => fee=20000*0.00036=7.2 => total=1007.2
    //   balance=47992.8-1007.2=46985.6
    account.place_order("ETHUSDT", 10.0, 2000.0, false, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 46985.6);

    // 建立行情map
    std::map<std::string, double> prices{
        {"BTCUSDT", 21000.0}, // BTC漲
        {"ETHUSDT", 1900.0}   // ETH跌 => 空單有利潤
    };

    account.update_positions(prices);

    // 計算預期未實現損益:
    //   BTC(多): entry=20000 => current=21000 => quantity=2 => (21000-20000)*2= +2000
    //   ETH(空): entry=2000 => current=1900 => quantity=10 => (2000-1900)*10= +1000
    // total_unrealized= +3000
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 3000.0);

    // equity= balance + total_unrealized=46985.6 + 3000= 49985.6
    EXPECT_DOUBLE_EQ(account.get_equity(), 49985.6);
}

//-------------------------------------------------------------------
// 7) 測試: update_positions - 缺少某 symbol 價格 => skip
//    - BTCUSDT 有價格 => PnL 會更新
//    - ETHUSDT 沒價格 => 會印 log, 不更新
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsMissingSymbolPrice) {
    Account account(30000.0, 0);

    // 下多單: BTCUSDT, 1@20000 => margin=1000 => fee=2 => bal=28998
    account.place_order("BTCUSDT", 1.0, 20000.0, true, true);

    // 下空單: ETHUSDT, 5@1500 => notional=7500 => margin=375 => fee=3 => total=378 => bal=28620
    account.place_order("ETHUSDT", 5.0, 1500.0, false, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 28620.0);

    // 價格 map 只提供 BTCUSDT
    std::map<std::string, double> priceMap{
        {"BTCUSDT", 21000.0}
        // ETHUSDT is missing
    };

    testing::internal::CaptureStderr();
    account.update_positions(priceMap);
    std::string logs = testing::internal::GetCapturedStderr();

    // BTC(多): entry=20000 => current=21000 => +1000
    // ETH(空): no price => skip => unrealized=0
    // total_unrealized=1000
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1000.0);
    // equity=28620+1000=29620
    EXPECT_DOUBLE_EQ(account.get_equity(), 29620.0);

    // 應該印出 "No price available for symbol: ETHUSDT"
    EXPECT_TRUE(logs.find("No price available for symbol: ETHUSDT") != std::string::npos);
}

//-------------------------------------------------------------------
// 8) 測試: update_positions - 觸發 Liquidation
//    - 多倉狀況下行情大幅反向
//    - verify balance=0, positions_清空
//-------------------------------------------------------------------
TEST_F(AccountTest, UpdatePositionsTriggerLiquidation) {
    // initial_balance=4000
    // 下多單: 5 BTC@1000 => notional=5000 => 20x => init_margin=250 => fee=1 => total=251 => bal=3749
    //   (為了更快達到維持保證金檢查，可再下單)
    Account account(4000.0, 0);
    account.place_order("BTCUSDT", 5.0, 1000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 3749.0);

    // 假設行情崩盤 => price=100 => 
    // BTC(多)= (100-1000)*5= -4500 => equity= 3749 +(-4500)= -751 <0
    // maint= notional(5000)*0.005=25 => equity <25 => => liquidation
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
// 9) 測試: close_position
//    - 平倉計算實現損益
//    - 釋放 initial_margin
//    - 扣除關倉手續費
//    - balance更新
//-------------------------------------------------------------------
TEST_F(AccountTest, ClosePosition) {
    Account account(10000.0, 0);

    // 下單: 1 BTC@5000 => notional=5000 => init_margin=250 => fee=1 => total=251 => bal=9749
    account.place_order("BTCUSDT", 1.0, 5000.0, true, false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9749.0);

    // update_positions => price=6000 => unrealized= +1000
    std::map<std::string, double> pMap{
        {"BTCUSDT", 6000.0}
    };
    account.update_positions(pMap);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1000.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 10749.0);

    // close_position => 
    //   close_fee= notional_close(6000*1)*makerFee(0.0002)=1.2
    //   realized_pnl= +1000
    //   amount_back= init_margin(250)+1000-1.2= 1248.8
    //   balance=9749 +1248.8= 10997.8
    testing::internal::CaptureStdout();
    account.close_position("BTCUSDT", 6000.0, false);
    std::string msg = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(msg.find("realized PnL=1000") != std::string::npos);
    EXPECT_NEAR(account.get_balance(), 10997.8, 1e-9);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 10997.8);

    // 再關同 symbol => "No position found"
    testing::internal::CaptureStdout();
    account.close_position("BTCUSDT", 6000.0, false);
    std::string msg2 = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(msg2.find("No position found for BTCUSDT") != std::string::npos);
}

//-------------------------------------------------------------------
// 10) 測試: adjust_position_leverage
//    - 在已有持倉的 symbol 上調整槓桿
//    - balance 會隨釋放或增加margin而變動
//    - 若餘額/權益不足, 失敗
//-------------------------------------------------------------------
TEST_F(AccountTest, AdjustPositionLeverage) {
    Account account(10000.0, 0);

    // 先下單: 1 BTC @4000 => notional=4000 => init_margin=200 => fee=0.8 => total=200.8 => bal=9799.2
    account.place_order("BTCUSDT", 1.0, 4000.0, true, true);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9799.2);

    // 改槓桿: 20=>10 => newMargin=400 => diff=+200 => bal=9799.2-200=9599.2
    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9599.2);

    // 再將槓桿10=>40 => newMargin=4000/40=100 => 釋放300 => bal=9599.2+300=9899.2
    account.set_symbol_leverage("BTCUSDT", 40.0);
    EXPECT_DOUBLE_EQ(account.get_balance(), 9899.2);

    // 若餘額不足 => 調小槓桿需要更多margin => 失敗
    // 先下單再追加大倉, 減少balance
    account.place_order("BTCUSDT", 5.0, 4000.0, true, true);
    // 5 BTC@4000 => notional=20000 => margin=20000/40=500 => fee=20000*0.0004=8 => total=508 => bal=9899.2-508=9391.2

    // 現在將槓桿從40 =>1 => newMargin=? 
    //   position(1BTC) => notional=4000
    //   position(5BTC) => notional=20000
    //   total notional=24000 => newMargin=24000/1=24000 => diff=24000 - ( (4000/40)+(20000/40) )= 24000- (100+500)=23400 => 需要23400
    //   但balance只有9391.2 + unrealized(暫時0)=9391.2 => 不足 => fail
    testing::internal::CaptureStderr();
    account.set_symbol_leverage("BTCUSDT", 1.0);
    std::string leverageLogs = testing::internal::GetCapturedStderr();

    // balance 不會變
    EXPECT_DOUBLE_EQ(account.get_balance(), 9391.2);
    // stderr => "Failed to change leverage"
    EXPECT_TRUE(leverageLogs.find("Failed to change leverage") != std::string::npos);
}

TEST_F(AccountTest, UpdatePositionsMultipleSymbolOneLiquidation) {
    // 假設初始餘額 5000 美金, VIP=0
    Account account(5000.0, 0);

    // 1) 下單：BTCUSDT 多單，數量很大 => 容易在行情不利時爆倉
    //    notional = 3 BTC * 2000 = 6000
    //    leverage 預設=20 => initial_margin=6000/20=300, fee=6000*0.0002=1.2 => total=301.2
    //    balance= 5000-301.2= 4698.8
    account.place_order("BTCUSDT", 3.0, 2000.0, true, /*is_market=*/false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 4698.8);

    // 2) 下單：ETHUSDT 空單，數量相對小 => 不易引發爆倉
    //    notional= 10 ETH * 1500= 15000
    //    leverage=20 => initial_margin=750 => fee=15000*0.0002=3 => total=753 => balance=3945.8
    account.place_order("ETHUSDT", 10.0, 1500.0, false, /*is_market=*/false);
    EXPECT_DOUBLE_EQ(account.get_balance(), 3945.8);

    // 先更新一次行情, 讓兩個倉位都有些小波動 (不爆倉)
    // BTCUSDT => 2100 (對多單有利), ETHUSDT => 1400 (對空單也有利 => entry=1500, current=1400 => 空單賺)
    std::map<std::string, double> pricesUp{
        {"BTCUSDT", 2100.0}, // BTC 從 2000 -> 2100 => + (2100-2000)*3= +300
        {"ETHUSDT", 1400.0}  // ETH 從 1500 -> 1400 => 空單 + (1500-1400)*10= +1000
    };
    account.update_positions(pricesUp);

    // total_unrealized_pnl= 300 + 1000= 1300
    // equity= 3945.8 + 1300= 5245.8 => 不爆倉
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 1300.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 5245.8);

    // 3) 現在讓 BTC 價格**猛烈下跌**，大幅不利多單
    //    同時 ETH 價格小幅上漲 (對空單不利，但幅度不大)
    //    假設 BTC = 100 => huge loss; ETH= 1550 => 小虧 => 看結果
    // BTC多單 => (100 - 2000)*3= -5700
    // ETH空單 => (1500 - 1550)*10= -500 (entry=1500, current=1550 => 空單 = entry-current>0 => actually negative)
    // total_unrealized= -6200
    // equity= balance(3945.8) + (-6200)= -2254.2 => < 0 => 一定 < maint => 爆
    std::map<std::string, double> crashPrices{
        {"BTCUSDT", 100.0},
        {"ETHUSDT", 1550.0}
    };

    testing::internal::CaptureStderr();
    account.update_positions(crashPrices);
    std::string liquidationLog = testing::internal::GetCapturedStderr();

    // 應該觸發全倉爆倉 => balance=0, positions 清空
    EXPECT_TRUE(liquidationLog.find("Liquidation triggered") != std::string::npos);
    EXPECT_DOUBLE_EQ(account.get_balance(), 0.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 0.0);

    // positions_ 皆被清空後，再做多餘檢查
    // (您可以修改 Account 類別讓它提供類似 get_positions() 的方法，或者以 friend 方式檢查，但這裡僅檢查 equity=0 即可)
}

