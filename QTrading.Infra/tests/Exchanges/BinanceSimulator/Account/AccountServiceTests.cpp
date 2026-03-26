#include <gtest/gtest.h>

#include <cmath>
#include <unordered_map>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace {

std::unordered_map<std::string, TradeKlineDto> one_trade_kline(
    const std::string& symbol,
    uint64_t ts,
    double open,
    double high,
    double low,
    double close,
    double volume)
{
    TradeKlineDto k{};
    k.Timestamp = ts;
    k.OpenPrice = open;
    k.HighPrice = high;
    k.LowPrice = low;
    k.ClosePrice = close;
    k.Volume = volume;
    return { { symbol, k } };
}

} // namespace

TEST(AccountServiceTests, TransferServiceMovesCashBetweenLedgersWithoutChangingTotalCash)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1200.0;
    cfg.perp_initial_wallet = 800.0;
    Account account(cfg);

    const double total_before = account.get_total_cash_balance();
    const uint64_t state_before = account.get_state_version();

    ASSERT_TRUE(account.transfer_spot_to_perp(200.0));
    ASSERT_TRUE(account.transfer_perp_to_spot(50.0));

    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 1050.0);
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 950.0);
    EXPECT_DOUBLE_EQ(account.get_total_cash_balance(), total_before);
    EXPECT_GT(account.get_state_version(), state_before);

    const uint64_t state_after_valid = account.get_state_version();
    EXPECT_FALSE(account.transfer_spot_to_perp(-1.0));
    EXPECT_FALSE(account.transfer_perp_to_spot(0.0));
    EXPECT_EQ(account.get_state_version(), state_after_valid);
}

TEST(AccountServiceTests, FundingServiceUpdatesWalletAndPreservesPositionShape)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 0.0;
    cfg.perp_initial_wallet = 1000.0;
    Account account(cfg);
    account.set_symbol_leverage("BTCUSDT", 10.0);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(one_trade_kline("BTCUSDT", 2000, 100.0, 100.0, 100.0, 100.0, 1000.0));

    const auto positions_before = account.get_all_positions();
    ASSERT_EQ(positions_before.size(), 1u);
    const int before_id = positions_before.front().id;
    const double before_qty = positions_before.front().quantity;
    const double wallet_before = account.get_wallet_balance();

    const auto funding = account.apply_funding("BTCUSDT", 1733497260000ull, 0.001, 100.0);
    ASSERT_EQ(funding.size(), 1u);
    const double funding_delta = funding.front().funding;

    const auto positions_after = account.get_all_positions();
    ASSERT_EQ(positions_after.size(), 1u);
    EXPECT_EQ(positions_after.front().id, before_id);
    EXPECT_NEAR(positions_after.front().quantity, before_qty, 1e-12);
    EXPECT_NEAR(account.get_wallet_balance(), wallet_before + funding_delta, 1e-9);
}

TEST(AccountServiceTests, OrderEntryAndMatchingPipelineStillEmitsFillEvents)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;
    Account account(cfg);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(one_trade_kline("BTCUSDT", 3000, 100.0, 100.0, 100.0, 100.0, 1000.0));

    auto fills = account.drain_fill_events();
    ASSERT_FALSE(fills.empty());
    EXPECT_TRUE(account.get_all_open_orders().empty());
    EXPECT_FALSE(account.get_all_positions().empty());
    EXPECT_NEAR(fills.back().total_cash_balance_snapshot, account.get_total_cash_balance(), 1e-9);
}
