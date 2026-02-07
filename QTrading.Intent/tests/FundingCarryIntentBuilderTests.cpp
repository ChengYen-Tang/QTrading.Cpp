#include "Intent/FundingCarryIntentBuilder.hpp"

#include <gtest/gtest.h>

TEST(FundingCarryIntentBuilderTests, BuildsReceiveFundingLegsOnActiveSignal)
{
    QTrading::Intent::FundingCarryIntentBuilder builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP", true });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Active;
    signal.urgency = QTrading::Signal::SignalUrgency::High;
    signal.strategy = "funding_carry";
    signal.ts_ms = 123;

    auto intent = builder.build(signal, nullptr);
    ASSERT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.legs[0].instrument, "BTCUSDT_SPOT");
    EXPECT_EQ(intent.legs[0].side, QTrading::Intent::TradeSide::Long);
    EXPECT_EQ(intent.legs[1].instrument, "BTCUSDT_PERP");
    EXPECT_EQ(intent.legs[1].side, QTrading::Intent::TradeSide::Short);
}

TEST(FundingCarryIntentBuilderTests, BuildsPayFundingLegsWhenConfigured)
{
    QTrading::Intent::FundingCarryIntentBuilder builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP", false });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Active;

    auto intent = builder.build(signal, nullptr);
    ASSERT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.legs[0].instrument, "BTCUSDT_SPOT");
    EXPECT_EQ(intent.legs[0].side, QTrading::Intent::TradeSide::Short);
    EXPECT_EQ(intent.legs[1].instrument, "BTCUSDT_PERP");
    EXPECT_EQ(intent.legs[1].side, QTrading::Intent::TradeSide::Long);
}

TEST(FundingCarryIntentBuilderTests, EmptyLegsWhenInactive)
{
    QTrading::Intent::FundingCarryIntentBuilder builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP", true });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Inactive;

    auto intent = builder.build(signal, nullptr);
    EXPECT_TRUE(intent.legs.empty());
}

TEST(FundingCarryIntentBuilderTests, UsesConfiguredCustomSymbols)
{
    QTrading::Intent::FundingCarryIntentBuilder builder({ "BTCUSDT_CASH", "BTCUSDT_SWAP", true });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Active;

    auto intent = builder.build(signal, nullptr);
    ASSERT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.legs[0].instrument, "BTCUSDT_CASH");
    EXPECT_EQ(intent.legs[1].instrument, "BTCUSDT_SWAP");
}
