#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include "Exchanges/BinanceSimulator/Futures/AccountPolicies.hpp"

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace {

static std::unordered_map<std::string, KlineDto> oneKline(
    const std::string& sym,
    double o, double h, double l, double c,
    double vol)
{
    KlineDto k;
    k.OpenPrice = o;
    k.HighPrice = h;
    k.LowPrice = l;
    k.ClosePrice = c;
    k.Volume = vol;
    return { {sym, k} };
}

} // namespace

TEST(AccountPoliciesTest, InjectedExecutionPriceIsUsed)
{
    Account::Policies p = AccountPolicies::Default();

    // Force every fill to happen at 123 regardless of kline/order.
    p.execution_price = [](const Order&, const KlineDto&, double, double) {
        return 123.0;
    };

    Account account(100000.0, 0, std::move(p));
    account.set_symbol_leverage("BTCUSDT", 10.0);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 0.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 1000.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_NEAR(pos[0].entry_price, 123.0, 1e-12);
}

TEST(AccountPoliciesTest, InjectedFeeRatesAffectChargedFeeRate)
{
    Account::Policies p = AccountPolicies::Default();

    // Make maker=10%, taker=20% so we can see the effect clearly.
    p.fee_rates = [](int) {
        return std::make_tuple(0.10, 0.20);
    };

    // Always taker so fee_rate should be 0.20.
    p.can_fill_and_taker = [](const Order&, const KlineDto&) {
        return std::make_pair(true, true);
    };

    Account account(100000.0, 0, std::move(p));
    account.set_symbol_leverage("BTCUSDT", 10.0);

    ASSERT_TRUE(account.place_order("BTCUSDT", 1.0, 0.0, OrderSide::Buy, PositionSide::Both));
    account.update_positions(oneKline("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1000.0));

    const auto& pos = account.get_all_positions();
    ASSERT_EQ(pos.size(), 1u);
    EXPECT_NEAR(pos[0].fee_rate, 0.20, 1e-12);

    // And wallet should be reduced by fee = notional * feeRate.
    // Fill price default is close=100, qty=1 => notional=100.
    // Fee = 20.
    EXPECT_NEAR(account.get_wallet_balance(), 100000.0 - 20.0, 1e-9);
}
