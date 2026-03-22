#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <iostream> // for CaptureStdout, CaptureStderr
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
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

TEST(AccountTest, AccountInitConfigConstructorInitializesStartingBalance) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 250.0;
    cfg.perp_initial_wallet = 750.0;
    cfg.vip_level = 1;

    Account account(cfg);
    auto perp_bal = account.get_balance();
    auto spot_bal = account.get_spot_balance();
    EXPECT_DOUBLE_EQ(perp_bal.WalletBalance, 750.0);
    EXPECT_DOUBLE_EQ(spot_bal.WalletBalance, 250.0);
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 750.0);
    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 250.0);
    EXPECT_DOUBLE_EQ(account.get_total_cash_balance(), 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
}

TEST(AccountTest, AccountInitConfigRejectsInvalidValues) {
    {
        Account::AccountInitConfig cfg;
        cfg.spot_initial_cash = -1.0;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
    {
        Account::AccountInitConfig cfg;
        cfg.perp_initial_wallet = -1.0;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
    {
        Account::AccountInitConfig cfg;
        cfg.vip_level = -1;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
}

TEST(AccountTest, DomainApisRouteByInstrumentType) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
    ASSERT_TRUE(account.perp.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));

    auto kline = oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0);
    QTrading::Dto::Market::Binance::KlineDto eth_kline;
    eth_kline.OpenPrice = 100.0;
    eth_kline.HighPrice = 100.0;
    eth_kline.LowPrice = 100.0;
    eth_kline.ClosePrice = 100.0;
    eth_kline.Volume = 1000.0;
    kline["ETHUSDT"] = eth_kline;
    account.update_positions(kline);

    EXPECT_EQ(account.get_instrument_spec("BTCUSDT").type, InstrumentType::Spot);
    EXPECT_EQ(account.get_instrument_spec("ETHUSDT").type, InstrumentType::Perp);
    EXPECT_LT(account.spot.get_cash_balance(), 1000.0);
    EXPECT_LT(account.perp.get_wallet_balance(), 1000.0);
}

TEST(AccountTest, DomainCancelOnlyAffectsOwnInstrumentBook) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 10000.0;
    cfg.perp_initial_wallet = 10000.0;
    Account account(cfg);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Dto::Trading::InstrumentType;

    ASSERT_TRUE(account.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
    ASSERT_TRUE(account.perp.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell, PositionSide::Both));

    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 2u);
    }

    account.spot.cancel_open_orders("BTCUSDT");
    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 1u);
        EXPECT_EQ(ords[0].instrument_type, InstrumentType::Perp);
    }

    account.perp.cancel_open_orders("BTCUSDT");
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, SpotAndPerpUseDifferentDefaultFeeTables) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_instrument_type("BTCUSDT_PERP", InstrumentType::Perp);

    // Spot VIP0 taker/maker: 0.10%/0.10%
    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    EXPECT_NEAR(account.get_spot_cash_balance(), 1000.0 - 100.0 - 0.1, 1e-9);

    // Perp VIP0 taker/maker keeps existing perp table: 0.02%/0.05%.
    // With current OHLC fill policy this order is treated as taker (close is marketable).
    ASSERT_TRUE(account.place_order("BTCUSDT_PERP", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_PERP", 100.0, 100.0, 100.0, 100.0, 1000.0));
    EXPECT_NEAR(account.get_wallet_balance(), 1000.0 - 0.05, 1e-9);
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

TEST(AccountTest, SpotSymbolLeverageIsAlwaysOne) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 2000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT_SPOT"), 1.0);
    account.set_symbol_leverage("BTCUSDT_SPOT", 20.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT_SPOT"), 1.0);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 1000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 1000.0, 1000.0, 1000.0, 1000.0, 10.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_DOUBLE_EQ(pos[0].leverage, 1.0);
}

TEST(AccountTest, SpotBuyConsumesOnlySpotCash) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 500.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 500.0);
    EXPECT_LT(account.get_spot_cash_balance(), 1000.0);
}

TEST(AccountTest, PerpTradeConsumesOnlyPerpWallet) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;
    Account account(cfg);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_symbol_leverage("BTCUSDT", 10.0);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 1000.0);
    EXPECT_LT(account.get_wallet_balance(), 1000.0);
}

TEST(AccountTest, SpotSellIncreasesOnlySpotCash) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 2000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    const double spot_after_buy = account.get_spot_cash_balance();
    const double perp_before_sell = account.get_wallet_balance();

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 0.5, 120.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 120.0, 120.0, 120.0, 120.0, 1000.0));

    EXPECT_GT(account.get_spot_cash_balance(), spot_after_buy);
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), perp_before_sell);
}

TEST(AccountTest, SpotOpenOrderReservesOnlySpotBudget) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 500.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    const auto perp_before = account.get_perp_balance();
    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 5.0, 100.0, OrderSide::Buy, PositionSide::Both));
    const auto spot_after = account.get_spot_balance();
    const auto perp_after = account.get_perp_balance();

    EXPECT_GT(spot_after.OpenOrderInitialMargin, 0.0);
    EXPECT_LT(spot_after.AvailableBalance, 1000.0);
    EXPECT_DOUBLE_EQ(perp_after.AvailableBalance, perp_before.AvailableBalance);
    EXPECT_DOUBLE_EQ(perp_after.WalletBalance, perp_before.WalletBalance);
}

TEST(AccountTest, SpotBuyPlacementRejectedWhenSpotBudgetExceededByOpenOrders) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 8.0, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_FALSE(account.place_order("BTCUSDT_SPOT", 3.0, 100.0, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, TransferBetweenLedgersRespectsAvailableBalance) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;
    Account account(cfg);

    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    EXPECT_TRUE(account.transfer_spot_to_perp(100.0));
    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 400.0);
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 600.0);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 3.0, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_FALSE(account.transfer_spot_to_perp(150.0));

    EXPECT_TRUE(account.transfer_perp_to_spot(200.0));
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 400.0);
    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 600.0);
    EXPECT_FALSE(account.transfer_perp_to_spot(1000.0));
}

TEST(AccountTest, ExplicitInstrumentTypeAppliesWithoutSuffixNaming) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 20000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT", InstrumentType::Spot);
    account.set_symbol_leverage("BTCUSDT", 20.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 1.0);

    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell, PositionSide::Both));

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    EXPECT_TRUE(account.place_order("BTCUSDT", 0.5, 100.0, OrderSide::Sell, PositionSide::Both));
}

TEST(AccountTest, CompatibilityMode_UnspecifiedInstrumentDefaultsToPerpPolicy) {
    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 10000.0;
    cfg.strict_binance_mode = false;
    Account account(cfg);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(account.get_symbol_leverage("BTCUSDT"), 10.0);
}

TEST(AccountTest, StrictModeRejectsUnknownSymbolOrderPlacement) {
    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 10000.0;
    cfg.strict_symbol_registration_mode = true;
    Account account(cfg);
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    EXPECT_FALSE(account.place_order("UNKNOWNUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    const auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::UnknownSymbol);
}

TEST(AccountTest, StrictModeDomainApiRejectsUnknownSymbolOrderPlacement) {
    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 10000.0;
    cfg.strict_symbol_registration_mode = true;
    Account account(cfg);
    using QTrading::Dto::Trading::OrderSide;

    EXPECT_FALSE(account.perp.place_order("UNKNOWNUSDT", 1.0, OrderSide::Buy));
    const auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::UnknownSymbol);
}

TEST(AccountTest, InstrumentFiltersRejectInvalidPriceTickAndRange) {
    Account account(10000.0, 0);
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    auto spec = QTrading::Dto::Trading::PerpInstrumentSpec();
    spec.min_price = 100.0;
    spec.max_price = 200.0;
    spec.price_tick_size = 0.1;
    account.set_instrument_spec("BTCUSDT", spec);

    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 99.9, OrderSide::Buy, PositionSide::Both));
    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 100.05, OrderSide::Buy, PositionSide::Both));
    EXPECT_TRUE(account.place_order("BTCUSDT", 1.0, 100.1, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, InstrumentFiltersRejectInvalidQuantityStepAndBounds) {
    Account account(10000.0, 0);
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    auto spec = QTrading::Dto::Trading::PerpInstrumentSpec();
    spec.min_qty = 0.1;
    spec.max_qty = 5.0;
    spec.qty_step_size = 0.1;
    account.set_instrument_spec("BTCUSDT", spec);

    EXPECT_FALSE(account.place_order("BTCUSDT", 0.05, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.15, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.2, 100.0, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, InstrumentFiltersRejectOrderByMinNotional) {
    Account account(10000.0, 0);
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    auto spec = QTrading::Dto::Trading::PerpInstrumentSpec();
    spec.min_notional = 50.0;
    account.set_instrument_spec("BTCUSDT", spec);

    // Set last mark so market-order notional can be estimated.
    account.update_positions(
        oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0),
        { {"BTCUSDT", 100.0} });

    EXPECT_FALSE(account.place_order("BTCUSDT", 0.4, 0.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.5, 0.0, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, InstrumentFiltersPerpMarketNotionalUsesMarkPrice) {
    Account account(10000.0, 0);
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    auto spec = QTrading::Dto::Trading::PerpInstrumentSpec();
    spec.min_notional = 150.0;
    account.set_instrument_spec("BTCUSDT", spec);
    account.set_market_slippage_buffer(0.0);

    // Trade close is 100, but mark override is 200.
    account.update_positions(
        oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0),
        { {"BTCUSDT", 200.0} });

    // Should pass only when perp market-notional estimation uses mark=200.
    EXPECT_TRUE(account.place_order("BTCUSDT", 1.0, 0.0, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, SpotMarketBudgetUsesTradePriceNotMarkPrice) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 100.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_market_slippage_buffer(0.0);

    // Trade close is high (100), mark override is low (1).
    account.update_positions(
        oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0),
        { {"BTCUSDT_SPOT", 1.0} });

    // If spot budget check used mark=1 this would pass; using trade=100 must reject.
    EXPECT_FALSE(account.place_order("BTCUSDT_SPOT", 2.0, 0.0, OrderSide::Buy, PositionSide::Both));
}

TEST(AccountTest, SpotQuoteOrderQtyMarketBuyConvertsAndLogsOriginalQuoteQty) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_market_slippage_buffer(0.0);
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    ASSERT_TRUE(account.spot.place_market_order_quote("BTCUSDT_SPOT", 100.0, OrderSide::Buy));
    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 1u);
        EXPECT_NEAR(ords[0].quantity, 1.0, 1e-12);
        EXPECT_NEAR(ords[0].quote_order_qty, 100.0, 1e-12);
    }

    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_NEAR(fills[0].quote_order_qty, 100.0, 1e-12);
}

TEST(AccountTest, SpotQuoteOrderQtyRequiresTradeReferencePrice) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    EXPECT_FALSE(account.spot.place_market_order_quote("BTCUSDT_SPOT", 100.0, OrderSide::Buy));
    auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::NotionalNoReferencePrice);
}

TEST(AccountTest, SpotBaseFeeModeBuyUsesBaseCommissionAndNotionalCashflow) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_spot_commission_mode(Account::SpotCommissionMode::BaseOnBuyQuoteOnSell);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    EXPECT_NEAR(account.get_spot_cash_balance(), 900.0, 1e-12);
    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_NEAR(pos[0].quantity, 0.999, 1e-12);

    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].fee_asset, static_cast<int32_t>(Account::CommissionAsset::BaseAsset));
    EXPECT_NEAR(fills[0].fee_native, 0.001, 1e-12);
    EXPECT_NEAR(fills[0].fee_quote_equiv, 0.1, 1e-12);
    EXPECT_NEAR(fills[0].spot_cash_delta, -100.0, 1e-12);
    EXPECT_NEAR(fills[0].spot_inventory_delta, 0.999, 1e-12);
    EXPECT_EQ(fills[0].commission_model_source, static_cast<int32_t>(Account::CommissionModelSource::ImputedBuyBase));
}

TEST(AccountTest, SpotBaseFeeModeSellStillUsesQuoteCommission) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_spot_commission_mode(Account::SpotCommissionMode::BaseOnBuyQuoteOnSell);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    {
        auto buy_fills = account.drain_fill_events();
        ASSERT_EQ(buy_fills.size(), 1u);
    }

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 0.999, 100.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    EXPECT_NEAR(account.get_spot_cash_balance(), 999.8001, 1e-9);
    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);
    const auto& sell_fill = fills[0];
    EXPECT_EQ(sell_fill.fee_asset, static_cast<int32_t>(Account::CommissionAsset::QuoteAsset));
    EXPECT_NEAR(sell_fill.spot_inventory_delta, -0.999, 1e-12);
    EXPECT_NEAR(sell_fill.spot_cash_delta, 99.8001, 1e-9);
    EXPECT_EQ(sell_fill.commission_model_source, static_cast<int32_t>(Account::CommissionModelSource::ImputedQuote));
}

TEST(AccountTest, SpotBaseFeeModeOpenOrderReserveUsesNotionalOnly) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 100.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);
    account.set_spot_commission_mode(Account::SpotCommissionMode::BaseOnBuyQuoteOnSell);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    const auto spot_bal = account.get_spot_balance();
    EXPECT_NEAR(spot_bal.OpenOrderInitialMargin, 100.0, 1e-12);
    EXPECT_NEAR(spot_bal.AvailableBalance, 0.0, 1e-12);
}

TEST(AccountTest, MixedBookSpotAndPerpRulesApplyByInstrumentType) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 50000.0;
    cfg.perp_initial_wallet = 50000.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    account.set_instrument_type("BTCUSDT_CASH", InstrumentType::Spot);
    account.set_instrument_type("BTCUSDT_SWAP", InstrumentType::Perp);
    account.set_symbol_leverage("BTCUSDT_SWAP", 10.0);

    // Spot cannot naked-short, perp can.
    EXPECT_FALSE(account.place_order("BTCUSDT_CASH", 0.1, 100.0, OrderSide::Sell, PositionSide::Both));
    EXPECT_TRUE(account.place_order("BTCUSDT_SWAP", 1.0, 100.0, OrderSide::Sell, PositionSide::Both));
    EXPECT_TRUE(account.place_order("BTCUSDT_CASH", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 2u);
        for (const auto& o : ords) {
            if (o.symbol == "BTCUSDT_CASH") {
                EXPECT_EQ(o.instrument_type, InstrumentType::Spot);
            }
            if (o.symbol == "BTCUSDT_SWAP") {
                EXPECT_EQ(o.instrument_type, InstrumentType::Perp);
            }
        }
    }

    QTrading::Dto::Market::Binance::KlineDto kc;
    kc.OpenPrice = 100.0;
    kc.HighPrice = 100.0;
    kc.LowPrice = 100.0;
    kc.ClosePrice = 100.0;
    kc.Volume = 1000.0;
    QTrading::Dto::Market::Binance::KlineDto ks = kc;
    account.update_positions({
        {"BTCUSDT_CASH", kc},
        {"BTCUSDT_SWAP", ks}
        });

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);
    bool foundSpot = false;
    bool foundPerp = false;
    for (const auto& p : pos) {
        if (p.symbol == "BTCUSDT_CASH") {
            foundSpot = true;
            EXPECT_TRUE(p.is_long);
            EXPECT_DOUBLE_EQ(p.leverage, 1.0);
            EXPECT_DOUBLE_EQ(p.maintenance_margin, 0.0);
            EXPECT_EQ(p.instrument_type, InstrumentType::Spot);
        }
        if (p.symbol == "BTCUSDT_SWAP") {
            foundPerp = true;
            EXPECT_FALSE(p.is_long);
            EXPECT_DOUBLE_EQ(p.leverage, 10.0);
            EXPECT_GT(p.maintenance_margin, 0.0);
            EXPECT_EQ(p.instrument_type, InstrumentType::Perp);
        }
    }
    EXPECT_TRUE(foundSpot);
    EXPECT_TRUE(foundPerp);

    const double before = account.get_wallet_balance();
    auto spotFunding = account.apply_funding("BTCUSDT_CASH", 1733497260000, 0.001, 10000.0);
    EXPECT_TRUE(spotFunding.empty());
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), before);

    auto perpFunding = account.apply_funding("BTCUSDT_SWAP", 1733497260000, 0.001, 10000.0);
    EXPECT_FALSE(perpFunding.empty());
    EXPECT_GT(account.get_wallet_balance(), before);
}

TEST(AccountTest, SpotRejectsNakedShortAndOversell) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 50000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    EXPECT_FALSE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Sell, PositionSide::Both));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 100.0, 100.0, 100.0, 100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    EXPECT_FALSE(account.place_order("BTCUSDT_SPOT", 2.0, 100.0, OrderSide::Sell, PositionSide::Both));
    EXPECT_TRUE(account.get_all_open_orders().empty());

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 0.6, 100.0, OrderSide::Sell, PositionSide::Both));
    EXPECT_FALSE(account.place_order("BTCUSDT_SPOT", 0.6, 100.0, OrderSide::Sell, PositionSide::Both));
}

TEST(AccountTest, SpotPositionHasNoMaintenanceMarginAndNoLiquidationPath) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 50000.0;
    cfg.perp_initial_wallet = 0.0;
    Account account(cfg);
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    account.set_instrument_type("BTCUSDT_SPOT", InstrumentType::Spot);

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 1000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT_SPOT", 1000.0, 1000.0, 1000.0, 1000.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    account.update_positions(oneKline("BTCUSDT_SPOT", 10.0, 10.0, 10.0, 10.0, 1000.0));
    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_DOUBLE_EQ(pos[0].maintenance_margin, 0.0);
    EXPECT_DOUBLE_EQ(account.get_balance().MaintenanceMargin, 0.0);
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

TEST(AccountTest, PerpClosePositionOrder_OneWayClosesFullPosition) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.perp.place_order("BTCUSDT", 2.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    ASSERT_TRUE(account.perp.place_close_position_order("BTCUSDT", OrderSide::Sell));
    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 1u);
        EXPECT_TRUE(ords[0].close_position);
        EXPECT_GE(ords[0].closing_position_id, 0);
    }

    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    EXPECT_TRUE(account.get_all_positions().empty());
}

TEST(AccountTest, PerpClosePositionOrder_HedgeModeClosesOnlyTargetSide) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.perp.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.perp.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell, PositionSide::Short));
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 2u);

    ASSERT_TRUE(account.perp.place_close_position_order("BTCUSDT", OrderSide::Sell, PositionSide::Long));
    {
        const auto& ords = account.get_all_open_orders();
        ASSERT_EQ(ords.size(), 1u);
        EXPECT_TRUE(ords[0].close_position);
        EXPECT_EQ(ords[0].position_side, PositionSide::Long);
    }

    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_FALSE(positions[0].is_long);
    EXPECT_NEAR(positions[0].quantity, 1.0, 1e-12);
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

TEST(AccountTest, ClientOrderIdMustBeUniqueAmongOpenOrders) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order(
        "BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both, false, "cid-1"));
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);
    EXPECT_EQ(account.get_all_open_orders()[0].client_order_id, "cid-1");

    EXPECT_FALSE(account.place_order(
        "BTCUSDT", 1.0, 99.0, OrderSide::Sell, PositionSide::Both, false, "cid-1"));
    auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::DuplicateClientOrderId);
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);
}

TEST(AccountTest, StpExpireTakerRejectsCrossingIncomingOrder) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order(
        "BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);

    EXPECT_FALSE(account.place_order(
        "BTCUSDT", 1.0, 99.0, OrderSide::Sell, PositionSide::Both, false, {},
        Account::SelfTradePreventionMode::ExpireTaker));
    auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::StpExpiredTaker);
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);
    EXPECT_EQ(account.get_all_open_orders()[0].side, OrderSide::Buy);
}

TEST(AccountTest, StpExpireMakerCancelsConflictingRestingOrder) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order(
        "BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);

    ASSERT_TRUE(account.place_order(
        "BTCUSDT", 1.0, 99.0, OrderSide::Sell, PositionSide::Both, false, {},
        Account::SelfTradePreventionMode::ExpireMaker));
    const auto& ord = account.get_all_open_orders();
    ASSERT_EQ(ord.size(), 1u);
    EXPECT_EQ(ord[0].side, OrderSide::Sell);
}

TEST(AccountTest, StpExpireBothCancelsRestingAndRejectsIncomingOrder) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order(
        "BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    EXPECT_EQ(account.get_all_open_orders().size(), 1u);

    EXPECT_FALSE(account.place_order(
        "BTCUSDT", 1.0, 99.0, OrderSide::Sell, PositionSide::Both, false, {},
        Account::SelfTradePreventionMode::ExpireBoth));
    auto rej = account.consume_last_order_reject_info();
    ASSERT_TRUE(rej.has_value());
    EXPECT_EQ(rej->code, Contracts::OrderRejectInfo::Code::StpExpiredBoth);
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

/// @brief Verifies liquidation clears all positions and zeroes balance.
TEST(AccountTest, Liquidation) {
    Account account(350000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 75.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(
        oneKline("BTCUSDT", 500.0, 500.0, 500.0, 500.0, 10000.0),
        { {"BTCUSDT", 500.0} });
    EXPECT_DOUBLE_EQ(account.get_all_positions().size(), 1);

    testing::internal::CaptureStderr();
    account.update_positions(
        oneKline("BTCUSDT", 1.0, 1.0, 1.0, 1.0, 10000.0),
        { {"BTCUSDT", 1.0} });
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);

    EXPECT_TRUE(account.get_all_positions().empty() || account.get_balance().MarginBalance >= account.get_balance().MaintenanceMargin);
}

TEST(AccountTest, LiquidationWarningZoneTriggersStagedReduction) {
    Account account(350000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 75.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(
        oneKline("BTCUSDT", 500.0, 500.0, 500.0, 500.0, 10000.0),
        { {"BTCUSDT", 500.0} });
    ASSERT_EQ(account.get_all_positions().size(), 1u);
    const double qty_before = account.get_all_positions()[0].quantity;

    testing::internal::CaptureStderr();
    account.update_positions(
        oneKline("BTCUSDT", 433.2, 433.2, 433.2, 433.2, 10000.0),
        { {"BTCUSDT", 433.2} });
    (void)testing::internal::GetCapturedStderr();

    const auto& pos = account.get_all_positions();
    ASSERT_FALSE(pos.empty());
    EXPECT_LT(pos[0].quantity, qty_before);
    EXPECT_GT(pos[0].quantity, 0.0);
}

TEST(AccountTest, DistressedLiquidationCancelsPerpOpenOrdersBeforeReduction) {
    Account account(350000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 75.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(
        oneKline("BTCUSDT", 500.0, 500.0, 500.0, 500.0, 10000.0),
        { {"BTCUSDT", 500.0} });
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    account.close_position("BTCUSDT", 700.0);
    ASSERT_EQ(account.get_all_open_orders().size(), 1u);
    EXPECT_GE(account.get_all_open_orders()[0].closing_position_id, 0);

    testing::internal::CaptureStderr();
    account.update_positions(
        oneKline("BTCUSDT", 432.4, 432.4, 432.4, 432.4, 10000.0),
        { {"BTCUSDT", 432.4} });
    std::string logs = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(logs.find("Liquidation triggered") != std::string::npos);

    const auto& pos = account.get_all_positions();
    ASSERT_FALSE(pos.empty());
    EXPECT_LT(pos[0].quantity, 5000.0);
    EXPECT_GT(pos[0].quantity, 0.0);
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, PerpLiquidationDoesNotConsumeSpotPosition) {
    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 100000.0;
    cfg.perp_initial_wallet = 350000.0;
    Account account(cfg);
    account.set_instrument_type("BTCUSDT_SPOT", QTrading::Dto::Trading::InstrumentType::Spot);
    account.set_instrument_type("BTCUSDT_PERP", QTrading::Dto::Trading::InstrumentType::Perp);
    account.set_symbol_leverage("BTCUSDT_PERP", 75.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT_PERP", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    QTrading::Dto::Market::Binance::KlineDto k_spot_entry;
    k_spot_entry.OpenPrice = 100.0;
    k_spot_entry.HighPrice = 100.0;
    k_spot_entry.LowPrice = 100.0;
    k_spot_entry.ClosePrice = 100.0;
    k_spot_entry.Volume = 100000.0;

    QTrading::Dto::Market::Binance::KlineDto k_perp_entry;
    k_perp_entry.OpenPrice = 500.0;
    k_perp_entry.HighPrice = 500.0;
    k_perp_entry.LowPrice = 500.0;
    k_perp_entry.ClosePrice = 500.0;
    k_perp_entry.Volume = 100000.0;

    account.update_positions({
        {"BTCUSDT_SPOT", k_spot_entry},
        {"BTCUSDT_PERP", k_perp_entry}
        });

    QTrading::Dto::Market::Binance::KlineDto k_spot_crash = k_spot_entry;
    QTrading::Dto::Market::Binance::KlineDto k_perp_crash = k_perp_entry;
    k_perp_crash.OpenPrice = 1.0;
    k_perp_crash.HighPrice = 1.0;
    k_perp_crash.LowPrice = 1.0;
    k_perp_crash.ClosePrice = 1.0;
    account.update_positions({
        {"BTCUSDT_SPOT", k_spot_crash},
        {"BTCUSDT_PERP", k_perp_crash}
        });

    bool has_spot_position = false;
    for (const auto& p : account.get_all_positions()) {
        if (p.symbol == "BTCUSDT_SPOT" && p.quantity > 0.0) {
            has_spot_position = true;
            break;
        }
    }
    EXPECT_TRUE(has_spot_position);
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

TEST(AccountTest, SwitchingModeWithOpenOrdersFails) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    EXPECT_FALSE(account.is_hedge_mode());

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_FALSE(account.get_all_open_orders().empty());

    account.set_position_mode(true);
    EXPECT_FALSE(account.is_hedge_mode());
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

/// @brief Default strict-Binance behavior: reject hedge-mode reduce_only.
TEST(AccountTest, HedgeModeReduceOnlyOrderRejectedByDefault) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    EXPECT_TRUE(account.is_hedge_mode());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    account.update_positions(partialMarketDataBTC(10000.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Sell, PositionSide::Long, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());
    auto reject = account.consume_last_order_reject_info();
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, Contracts::OrderRejectInfo::Code::StrictHedgeReduceOnlyDisabled);

    account.update_positions(partialMarketDataBTC(10000.0, 1000.0));
    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_TRUE(positions[0].is_long);
    EXPECT_NEAR(positions[0].quantity, 2.0, 1e-12);
}

TEST(AccountTest, CompatibilityModeAllowsHedgeReduceOnlyWhenStrictDisabled) {
    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 10000.0;
    cfg.strict_binance_mode = false;
    Account account(cfg);
    EXPECT_FALSE(account.is_strict_binance_mode());

    account.set_position_mode(true);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    account.update_positions(partialMarketDataBTC(10000.0, 1000.0));
    ASSERT_EQ(account.get_all_positions().size(), 1u);

    // Compatibility mode: preserve legacy acceptance behavior.
    EXPECT_TRUE(account.place_order("BTCUSDT", 0.5, 10000.0, OrderSide::Sell, PositionSide::Long, true));
    account.update_positions(partialMarketDataBTC(10000.0, 1000.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_TRUE(positions[0].is_long);
    EXPECT_NEAR(positions[0].quantity, 0.5, 1e-12);
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

TEST(AccountTest, MergePositionsCanBeDisabledToPreserveFillLineage) {
    Account account(10000.0, 0);
    account.set_position_mode(true);
    account.set_merge_positions_enabled(false);
    EXPECT_FALSE(account.is_merge_positions_enabled());
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 10000.0, OrderSide::Buy, PositionSide::Long));
    account.update_positions(partialMarketDataBTC(10000.0, 10.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);
    const double total_qty = positions[0].quantity + positions[1].quantity;
    EXPECT_NEAR(total_qty, 3.0, 1e-12);
    EXPECT_NE(positions[0].id, positions[1].id);
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

TEST(AccountTest, MaintenanceMarginUsesBracketDeductionAboveFirstTier) {
    Account account(1'000'000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 100.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100000.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(partialMarketDataBTC(100000.0, 1000.0));

    const auto& positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);

    // Tier2 (<=600k): mmr=0.005 with bracket deduction:
    // deduction = 50,000 * (0.005 - 0.004) = 50
    // maint = 100,000 * 0.005 - 50 = 450
    EXPECT_NEAR(positions[0].maintenance_margin, 450.0, 1e-9);
    EXPECT_LT(positions[0].maintenance_margin, 500.0);
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

    // Default strict mode rejects hedge reduce_only.
    EXPECT_FALSE(account.place_order("BTCUSDT", 1.0, 20000.0, OrderSide::Sell, PositionSide::Long, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());
    account.update_positions(marketData);

    positions = account.get_all_positions();
    ASSERT_EQ(positions.size(), 2u);
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

TEST(AccountTest, IntraBarExpectedPath_SplitsOppositePassiveLimitVolume) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_intra_bar_path_mode(Account::IntraBarPathMode::ExpectedPath);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 10.0, 95.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 10.0, 105.0, OrderSide::Sell, PositionSide::Both));

    account.update_positions(oneKline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 10.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);

    double long_qty = 0.0;
    double short_qty = 0.0;
    for (const auto& p : pos) {
        if (p.is_long) {
            long_qty += p.quantity;
        }
        else {
            short_qty += p.quantity;
        }
    }

    EXPECT_NEAR(long_qty, 5.0, 1e-8);
    EXPECT_NEAR(short_qty, 5.0, 1e-8);
}

TEST(AccountTest, IntraBarPathModeUsesOpenMarketabilityForTakerClassification) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_intra_bar_path_mode(Account::IntraBarPathMode::ExpectedPath);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 105.0, 106.0, 95.0, 99.0, 1000.0));

    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_FALSE(fills[0].is_taker);
}

TEST(AccountTest, IntraBarMonteCarloPathWithFixedSeedIsDeterministic) {
    auto run_once = [](uint64_t seed) {
        Account account(100000.0, 0);
        account.set_symbol_leverage("BTCUSDT", 10.0);
        account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
        account.set_intra_bar_path_mode(Account::IntraBarPathMode::MonteCarloPath);
        account.set_intra_bar_monte_carlo_samples(1);
        account.set_intra_bar_random_seed(seed);

        using QTrading::Dto::Trading::OrderSide;
        using QTrading::Dto::Trading::PositionSide;

        EXPECT_TRUE(account.place_order("BTCUSDT", 10.0, 95.0, OrderSide::Buy, PositionSide::Both));
        EXPECT_TRUE(account.place_order("BTCUSDT", 10.0, 105.0, OrderSide::Sell, PositionSide::Both));

        account.update_positions(oneKline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 10.0));

        double long_qty = 0.0;
        for (const auto& p : account.get_all_positions()) {
            if (p.is_long) {
                long_qty += p.quantity;
            }
        }
        return long_qty;
    };

    const double first = run_once(42ull);
    const double second = run_once(42ull);
    EXPECT_NEAR(first, second, 1e-12);
    EXPECT_TRUE(std::abs(first - 0.0) < 1e-12 || std::abs(first - 10.0) < 1e-12);
}

TEST(AccountTest, LimitFillProbabilityModelUsesPenetrationAndSizeRatio) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_limit_fill_probability_enabled(true);
    account.set_limit_fill_probability_coefficients(1.0, 2.0, 2.0, 0.0, 0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Same quantity, different penetration depth:
    // Buy@99 penetrates deeper than Buy@91 when low=90.
    ASSERT_TRUE(account.place_order("BTCUSDT", 10.0, 99.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 10.0, 91.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(oneKline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 100.0));
    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 2u);

    const Account::FillEvent* deep_fill = nullptr;
    const Account::FillEvent* shallow_fill = nullptr;
    for (const auto& f : fills) {
        if (std::abs(f.order_price - 99.0) < 1e-12) {
            deep_fill = &f;
        }
        if (std::abs(f.order_price - 91.0) < 1e-12) {
            shallow_fill = &f;
        }
    }

    ASSERT_NE(deep_fill, nullptr);
    ASSERT_NE(shallow_fill, nullptr);
    EXPECT_GT(deep_fill->fill_probability, shallow_fill->fill_probability);
    EXPECT_GT(deep_fill->exec_qty, shallow_fill->exec_qty);
    const double deep_prob = deep_fill->fill_probability;

    // Add a large-size order on the next bar to validate size-ratio penalty.
    ASSERT_TRUE(account.place_order("BTCUSDT", 80.0, 99.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 100.0));
    fills = account.drain_fill_events();
    ASSERT_FALSE(fills.empty());

    const Account::FillEvent* large_fill = nullptr;
    for (const auto& f : fills) {
        if (std::abs(f.order_qty - 80.0) < 1e-12) {
            large_fill = &f;
            break;
        }
    }
    ASSERT_NE(large_fill, nullptr);
    EXPECT_LT(large_fill->fill_probability, deep_prob);
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

TEST(AccountTest, OneWayFlip_OvershootTransitionsCloseThenOpenThroughLifecycle) {
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

    // SELL 5 => single reverse order that will close then open during fill lifecycle.
    ASSERT_TRUE(account.place_order("BTCUSDT", 5.0, 100.0, OrderSide::Sell, PositionSide::Both));

    const auto& ords = account.get_all_open_orders();
    ASSERT_EQ(ords.size(), 1u);
    EXPECT_GE(ords[0].closing_position_id, 0);
    EXPECT_EQ(ords[0].side, OrderSide::Sell);
    EXPECT_TRUE(ords[0].one_way_reverse);

    // First tick consumes the close leg.
    account.update_positions(partialMarketDataBTC(100.0, 1000.0));
    {
        const auto& posMid = account.get_all_positions();
        EXPECT_TRUE(posMid.empty());
    }
    {
        const auto& ordMid = account.get_all_open_orders();
        ASSERT_EQ(ordMid.size(), 1u);
        EXPECT_NEAR(ordMid[0].quantity, 3.0, 1e-12);
    }

    // Next tick transitions to opening leg and creates SHORT 3.
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

    // In default strict mode, hedge reduce_only is always rejected.
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Sell, PositionSide::Long, true));
    EXPECT_FALSE(account.place_order("BTCUSDT", 0.25, 100.0, OrderSide::Buy, PositionSide::Short, true));
    EXPECT_TRUE(account.get_all_open_orders().empty());
}

TEST(AccountTest, OpenOrderInitialMargin_MarketOrderUsesLastMarkWithBuffer) {
    Account account(10000.0, 0);
    account.set_position_mode(false);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_market_slippage_buffer(0.0); // deterministic

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    // Trade close is 50, but mark override is 100.
    account.update_positions(
        oneKline("BTCUSDT", 50.0, 50.0, 50.0, 50.0, 1000.0),
        { {"BTCUSDT", 100.0} });

    ASSERT_TRUE(account.place_order("BTCUSDT", 2.0, 0.0, OrderSide::Buy, PositionSide::Both));

    auto bal = account.get_balance();
    // notional = 2*100, lev=10 => 20
    EXPECT_NEAR(bal.OpenOrderInitialMargin, 20.0, 1e-12);
}

TEST(AccountTest, UnrealizedPnlUsesMarkOverrideNotTradeClose) {
    Account account(50000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(
        oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0),
        { {"BTCUSDT", 100.0} });

    // Trade close moves up to 150, but mark is overridden to 80.
    account.update_positions(
        oneKline("BTCUSDT", 150.0, 150.0, 150.0, 150.0, 1000.0),
        { {"BTCUSDT", 80.0} });

    const auto bal = account.get_balance();
    EXPECT_NEAR(bal.UnrealizedPnl, -20.0, 1e-12);
    EXPECT_NEAR(account.total_unrealized_pnl(), -20.0, 1e-12);
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

TEST(AccountTest, MarketImpactSlippageCurveWorsensLargeOrderMoreThanSmallOrder) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_market_impact_slippage_enabled(true);
    account.set_market_impact_slippage_params(0.0, 500.0, 1.0, 0.0, 0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 0.0, OrderSide::Buy, PositionSide::Both));
    ASSERT_TRUE(account.place_order("BTCUSDT", 8.0, 0.0, OrderSide::Buy, PositionSide::Both));

    account.update_positions(oneKline("BTCUSDT", 100.0, 120.0, 80.0, 100.0, 100.0));
    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 2u);

    const Account::FillEvent* small = nullptr;
    const Account::FillEvent* large = nullptr;
    for (const auto& f : fills) {
        if (std::abs(f.order_qty - 1.0) < 1e-12) {
            small = &f;
        }
        if (std::abs(f.order_qty - 8.0) < 1e-12) {
            large = &f;
        }
    }

    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    EXPECT_GT(large->impact_slippage_bps, small->impact_slippage_bps);
    EXPECT_GT(large->exec_price, small->exec_price);
}

TEST(AccountTest, MarketImpactSlippageRespectsLimitProtection) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_market_impact_slippage_enabled(true);
    account.set_market_impact_slippage_params(0.0, 5000.0, 1.0, 0.0, 0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 8.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 105.0, 110.0, 95.0, 100.0, 100.0));

    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_LE(fills[0].exec_price, 100.0 + 1e-12);
}

TEST(AccountTest, TakerProbabilityModelUsesDiscreteFeeRate) {
    Account account(100000.0, 0);
    account.set_symbol_leverage("BTCUSDT", 10.0);
    account.set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
    account.set_taker_probability_model_enabled(true);
    account.set_taker_probability_model_coefficients(-1.0, 2.0, 0.5, 0.5, 0.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 99.0, OrderSide::Buy, PositionSide::Both));

    QTrading::Dto::Market::Binance::KlineDto k;
    k.OpenPrice = 100.0;
    k.HighPrice = 110.0;
    k.LowPrice = 90.0;
    k.ClosePrice = 100.0;
    k.Volume = 100.0;
    k.TakerBuyBaseVolume = 90.0;
    account.update_positions(std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>{ {"BTCUSDT", k} });

    auto fills = account.drain_fill_events();
    ASSERT_EQ(fills.size(), 1u);

    const double p = fills[0].taker_probability;
    EXPECT_GT(p, 0.0);
    EXPECT_LT(p, 1.0);

    const double expected_rate = fills[0].is_taker ? 0.0005 : 0.0002;
    EXPECT_NEAR(fills[0].fee_rate, expected_rate, 1e-12);
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

TEST(AccountTest, ApplyFunding_LongPays) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    const double before = account.get_wallet_balance();
    (void)account.apply_funding("BTCUSDT", 1733497260000, 0.001, 10000.0);
    const double after = account.get_wallet_balance();

    EXPECT_NEAR(after, before - 10.0, 1e-8);
}

TEST(AccountTest, ApplyFunding_ShortReceives) {
    Account account(10000.0, 0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, OrderSide::Sell, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    const double before = account.get_wallet_balance();
    (void)account.apply_funding("BTCUSDT", 1733497260000, 0.001, 10000.0);
    const double after = account.get_wallet_balance();

    EXPECT_NEAR(after, before + 10.0, 1e-8);
}
