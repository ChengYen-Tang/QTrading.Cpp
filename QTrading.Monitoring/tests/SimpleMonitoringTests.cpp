#include "Monitoring/SimpleMonitoring.hpp"

#include <gtest/gtest.h>

TEST(SimpleMonitoringTests, WarnsOnTooManyOrders)
{
    QTrading::Monitoring::SimpleMonitoring monitoring({ 2 });
    QTrading::Risk::AccountState account;

    QTrading::dto::Order o1{};
    o1.symbol = "BTCUSDT_PERP";
    QTrading::dto::Order o2 = o1;
    QTrading::dto::Order o3 = o1;
    account.open_orders = { o1, o2, o3 };

    auto alerts = monitoring.check(account);
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].symbol, "BTCUSDT_PERP");
    EXPECT_EQ(alerts[0].action, "CANCEL_OPEN_ORDERS");
}
