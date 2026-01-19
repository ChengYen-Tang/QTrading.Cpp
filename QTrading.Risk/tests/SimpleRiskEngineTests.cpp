#include "Risk/SimpleRiskEngine.hpp"

#include <gtest/gtest.h>

TEST(SimpleRiskEngineTests, ProducesTargetsForIntentLegs)
{
    QTrading::Risk::SimpleRiskEngine engine({ 1000.0, 2.0, 3.0 });
    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_zscore";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto out = engine.position(intent, account, nullptr);

    EXPECT_EQ(out.target_positions["BTCUSDT_SPOT"], 1000.0);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], -1000.0);
    EXPECT_EQ(out.leverage["BTCUSDT_SPOT"], 2.0);
    EXPECT_EQ(out.leverage["BTCUSDT_PERP"], 2.0);
}

TEST(SimpleRiskEngineTests, FlattensExistingPositionsWhenNoIntent)
{
    QTrading::Risk::SimpleRiskEngine engine({ 1000.0, 2.0, 3.0 });
    QTrading::Intent::TradeIntent intent;
    QTrading::Risk::AccountState account;

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    account.positions.push_back(pos);

    auto out = engine.position(intent, account, nullptr);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], 0.0);
}
