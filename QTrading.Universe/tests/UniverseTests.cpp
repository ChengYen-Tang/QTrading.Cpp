#include "Universe/IUniverseSelector.hpp"

#include <gtest/gtest.h>

TEST(NullUniverseSelectorTests, ReturnsEmptyUniverse)
{
    QTrading::Universe::NullUniverseSelector selector;
    auto sel = selector.select();
    EXPECT_TRUE(sel.universe.empty());
}
