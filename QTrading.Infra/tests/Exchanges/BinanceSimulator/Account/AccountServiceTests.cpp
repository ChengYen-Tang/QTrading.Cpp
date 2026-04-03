#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Infra::Exchanges::BinanceSim::Account;

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
