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
