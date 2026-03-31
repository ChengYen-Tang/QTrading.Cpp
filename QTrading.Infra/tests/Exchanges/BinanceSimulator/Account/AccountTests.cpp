#include <gtest/gtest.h>

#include <stdexcept>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Infra::Exchanges::BinanceSim::Account;

TEST(AccountTest, ConstructorAndGetters)
{
    Account account(1000.0, 0);
    const auto bal = account.get_perp_balance();
    EXPECT_DOUBLE_EQ(bal.WalletBalance, 1000.0);
    EXPECT_DOUBLE_EQ(account.total_unrealized_pnl(), 0.0);
    EXPECT_DOUBLE_EQ(account.get_equity(), 1000.0);
}

TEST(AccountTest, AccountInitConfigConstructorInitializesStartingBalance)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 250.0;
    cfg.perp_initial_wallet = 750.0;

    Account account(cfg);

    const auto spot = account.get_spot_balance();
    const auto perp = account.get_perp_balance();
    EXPECT_DOUBLE_EQ(spot.WalletBalance, 250.0);
    EXPECT_DOUBLE_EQ(spot.AvailableBalance, 250.0);
    EXPECT_DOUBLE_EQ(perp.WalletBalance, 750.0);
    EXPECT_DOUBLE_EQ(perp.AvailableBalance, 750.0);
    EXPECT_DOUBLE_EQ(account.get_total_cash_balance(), 1000.0);
}

TEST(AccountTest, AccountInitConfigRejectsInvalidValues)
{
    {
        Account::AccountInitConfig cfg{};
        cfg.spot_initial_cash = -1.0;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
    {
        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = -1.0;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
    {
        Account::AccountInitConfig cfg{};
        cfg.vip_level = -1;
        EXPECT_THROW((void)Account(cfg), std::runtime_error);
    }
}

TEST(AccountTest, TransferBetweenLedgersRejectsOnlyInsufficientLedgerCashInReducedScope)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;
    Account account(cfg);

    EXPECT_TRUE(account.transfer_spot_to_perp(100.0));
    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 400.0);
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 600.0);
    EXPECT_FALSE(account.transfer_spot_to_perp(500.0));

    EXPECT_TRUE(account.transfer_perp_to_spot(200.0));
    EXPECT_DOUBLE_EQ(account.get_wallet_balance(), 400.0);
    EXPECT_DOUBLE_EQ(account.get_spot_cash_balance(), 600.0);
    EXPECT_FALSE(account.transfer_perp_to_spot(1000.0));
    EXPECT_DOUBLE_EQ(account.get_total_cash_balance(), 1000.0);
}
