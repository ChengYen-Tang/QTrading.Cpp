#include "Intent/BasisIntentBuilder.hpp"

#include <gtest/gtest.h>

TEST(BasisIntentBuilderTests, BuildsLegsOnActiveSignal)
{
    QTrading::Intent::BasisIntentBuilder builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Active;
    signal.urgency = QTrading::Signal::SignalUrgency::High;
    signal.strategy = "basis_zscore";
    signal.ts_ms = 123;

    auto intent = builder.build(signal, nullptr);
    ASSERT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.legs[0].instrument, "BTCUSDT_SPOT");
    EXPECT_EQ(intent.legs[0].side, QTrading::Intent::TradeSide::Long);
    EXPECT_EQ(intent.legs[1].instrument, "BTCUSDT_PERP");
    EXPECT_EQ(intent.legs[1].side, QTrading::Intent::TradeSide::Short);
}

TEST(BasisIntentBuilderTests, EmptyLegsWhenInactive)
{
    QTrading::Intent::BasisIntentBuilder builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    QTrading::Signal::SignalDecision signal;
    signal.status = QTrading::Signal::SignalStatus::Inactive;

    auto intent = builder.build(signal, nullptr);
    EXPECT_TRUE(intent.legs.empty());
}
