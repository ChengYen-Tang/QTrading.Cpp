#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace {

using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

std::vector<BinanceExchange::SymbolDataset> MakeDatasets()
{
    return QTrading::Infra::Exchanges::BinanceSim::Bootstrap::ToDatasets(
        { { "BTCUSDT", "btc.csv" } });
}

TEST(BinanceExchangeSkeletonTests, ConstructorPreservesFacadeAndChannelsExist)
{
    BinanceExchange exchange(MakeDatasets(), nullptr,
        QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));

    EXPECT_NE(exchange.get_market_channel(), nullptr);
    EXPECT_NE(exchange.get_position_channel(), nullptr);
    EXPECT_NE(exchange.get_order_channel(), nullptr);
    EXPECT_TRUE(exchange.get_all_positions().empty());
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
    EXPECT_DOUBLE_EQ(exchange.account.get_total_cash_balance(), 1000.0);
}

TEST(BinanceExchangeSkeletonTests, StepThrowsNotImplemented)
{
    BinanceExchange exchange(MakeDatasets(), nullptr,
        QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));

    EXPECT_THROW(static_cast<void>(exchange.step()), std::runtime_error);
}

} // namespace
