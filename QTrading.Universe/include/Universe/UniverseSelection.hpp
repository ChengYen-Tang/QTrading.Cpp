#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace QTrading::Universe {

/// @brief Selected tradable symbols at a given timestamp.
struct UniverseSelection {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Allowed symbols for this step.
    std::vector<std::string> universe;
};

} // namespace QTrading::Universe
