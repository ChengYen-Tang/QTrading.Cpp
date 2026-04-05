#include "Universe/FixedUniverseSelector.hpp"

#include <gtest/gtest.h>

TEST(FixedUniverseSelectorTests, ReturnsFixedSymbols)
{
    QTrading::Universe::FixedUniverseSelector selector({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    auto sel = selector.select();
    ASSERT_EQ(sel.universe.size(), 2u);
    EXPECT_EQ(sel.universe[0], "BTCUSDT_SPOT");
    EXPECT_EQ(sel.universe[1], "BTCUSDT_PERP");
}

TEST(FixedUniverseSelectorTests, ReturnsEmptyUniverseWhenSymbolsOmitted)
{
    QTrading::Universe::FixedUniverseSelector selector({});
    auto sel = selector.select();
    EXPECT_TRUE(sel.universe.empty());
}

TEST(NullUniverseSelectorTests, ReturnsEmptyUniverse)
{
    QTrading::Universe::NullUniverseSelector selector;
    auto sel = selector.select();
    EXPECT_TRUE(sel.universe.empty());
}
