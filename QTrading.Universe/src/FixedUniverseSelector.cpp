#include "Universe/FixedUniverseSelector.hpp"

#include <utility>

namespace QTrading::Universe {

FixedUniverseSelector::FixedUniverseSelector(std::vector<std::string> symbols)
    : symbols_(std::move(symbols))
{
}

UniverseSelection FixedUniverseSelector::select()
{
    UniverseSelection out;
    out.universe = symbols_;
    return out;
}

} // namespace QTrading::Universe
