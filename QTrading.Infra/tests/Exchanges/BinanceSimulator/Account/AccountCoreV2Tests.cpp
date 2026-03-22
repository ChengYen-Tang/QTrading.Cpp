#include <gtest/gtest.h>

#include <cmath>
#include <unordered_map>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountCoreV2.hpp"

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace {

std::unordered_map<std::string, TradeKlineDto> one_kline(
    const std::string& symbol,
    uint64_t ts,
    double open,
    double high,
    double low,
    double close,
    double vol)
{
    TradeKlineDto k{};
    k.Timestamp = ts;
    k.OpenPrice = open;
    k.HighPrice = high;
    k.LowPrice = low;
    k.ClosePrice = close;
    k.Volume = vol;
    return { { symbol, k } };
}

} // namespace

TEST(AccountCoreV2Tests, SnapshotStateViewExposesStableReadModel)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;

    Account account(cfg);
    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(one_kline("BTCUSDT", 1000, 100.0, 100.0, 100.0, 100.0, 1000.0));

    const auto view = account.snapshot_state_view();
    EXPECT_GE(view.state_version, 1u);
    EXPECT_FALSE(view.hedge_mode);
    EXPECT_NEAR(view.total_cash_balance, account.get_total_cash_balance(), 1e-9);
    EXPECT_NEAR(view.equity, account.get_equity(), 1e-9);
    ASSERT_EQ(view.positions.size(), 1u);
    EXPECT_EQ(view.positions.front().symbol, "BTCUSDT");
    EXPECT_NEAR(view.positions.front().quantity, 1.0, 1e-9);
    EXPECT_TRUE(view.open_orders.empty());
}

TEST(AccountCoreV2Tests, ApplyCommandPreservesLegacyTradingSemanticsForOrderLifecycle)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    Account legacy(cfg);
    AccountCoreV2 v2(cfg);

    ASSERT_TRUE(legacy.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));

    AccountCoreV2::Command cmd{};
    cmd.kind = AccountCoreV2::CommandKind::PlaceLimitOrder;
    cmd.symbol = "BTCUSDT";
    cmd.quantity = 1.0;
    cmd.price = 100.0;
    cmd.side = OrderSide::Buy;
    cmd.position_side = PositionSide::Both;
    const auto place_result = v2.apply_command(cmd);
    ASSERT_TRUE(place_result.accepted);
    ASSERT_FALSE(place_result.reject_info.has_value());

    const auto market = one_kline("BTCUSDT", 2000, 100.0, 100.0, 100.0, 100.0, 1000.0);
    legacy.update_positions(market);
    v2.mutable_legacy_account().update_positions(market);

    const auto legacy_view = legacy.snapshot_state_view();
    const auto v2_view = v2.snapshot_state();

    EXPECT_NEAR(v2_view.total_cash_balance, legacy_view.total_cash_balance, 1e-9);
    EXPECT_NEAR(v2_view.perp_wallet_balance, legacy_view.perp_wallet_balance, 1e-9);
    EXPECT_NEAR(v2_view.equity, legacy_view.equity, 1e-9);
    ASSERT_EQ(v2_view.positions.size(), legacy_view.positions.size());
    ASSERT_EQ(v2_view.positions.size(), 1u);
    EXPECT_EQ(v2_view.positions.front().symbol, legacy_view.positions.front().symbol);
    EXPECT_NEAR(v2_view.positions.front().quantity, legacy_view.positions.front().quantity, 1e-9);
}

TEST(AccountCoreV2Tests, ApplyCommandReturnsRejectInfoForInvalidOrder)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;
    AccountCoreV2 v2(cfg);

    AccountCoreV2::Command cmd{};
    cmd.kind = AccountCoreV2::CommandKind::PlaceLimitOrder;
    cmd.symbol = "BTCUSDT";
    cmd.quantity = -1.0;
    cmd.price = 100.0;
    cmd.side = OrderSide::Buy;

    const auto result = v2.apply_command(cmd);
    EXPECT_FALSE(result.accepted);
    ASSERT_TRUE(result.reject_info.has_value());
    EXPECT_EQ(result.reject_info->code, Contracts::OrderRejectInfo::Code::InvalidQuantity);
}

TEST(AccountCoreV2Tests, ApplyFundingCommandMatchesLegacyFundingEffect)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    Account legacy(cfg);
    AccountCoreV2 v2(cfg);

    ASSERT_TRUE(legacy.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy, PositionSide::Both));
    AccountCoreV2::Command place{};
    place.kind = AccountCoreV2::CommandKind::PlaceLimitOrder;
    place.symbol = "BTCUSDT";
    place.quantity = 1.0;
    place.price = 100.0;
    place.side = OrderSide::Buy;
    ASSERT_TRUE(v2.apply_command(place).accepted);

    const auto market = one_kline("BTCUSDT", 3000, 100.0, 100.0, 100.0, 100.0, 1000.0);
    legacy.update_positions(market);
    v2.mutable_legacy_account().update_positions(market);

    const auto legacy_funding = legacy.apply_funding("BTCUSDT", 4000, 0.001, 100.0);

    AccountCoreV2::Command funding{};
    funding.kind = AccountCoreV2::CommandKind::ApplyFunding;
    funding.symbol = "BTCUSDT";
    funding.funding_time = 4000;
    funding.funding_rate = 0.001;
    funding.mark_price = 100.0;
    const auto v2_funding = v2.apply_command(funding);

    EXPECT_EQ(v2_funding.funding_results.size(), legacy_funding.size());
    const auto legacy_view = legacy.snapshot_state_view();
    const auto v2_view = v2.snapshot_state();
    EXPECT_NEAR(v2_view.perp_wallet_balance, legacy_view.perp_wallet_balance, 1e-9);
    EXPECT_NEAR(v2_view.total_cash_balance, legacy_view.total_cash_balance, 1e-9);
}

