#include "Universe/FixedUniverseSelector.hpp"

#include <utility>

namespace QTrading::Universe {

namespace {
std::vector<std::string> DefaultFundingCarryUniverse()
{
    return { "BTCUSDT_SPOT", "BTCUSDT_PERP" };
}
} // namespace

FixedUniverseSelector::FixedUniverseSelector(std::vector<std::string> symbols)
    : symbols_(symbols.empty() ? DefaultFundingCarryUniverse() : std::move(symbols))
{
}

UniverseSelection FixedUniverseSelector::select()
{
    UniverseSelection out;
    out.universe = symbols_;
    return out;
}

} // namespace QTrading::Universe
