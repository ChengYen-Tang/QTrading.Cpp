#pragma once

#include "UniverseSelection.hpp"

namespace QTrading::Universe {

/// @brief Interface for selecting the tradable universe.
class IUniverseSelector {
public:
    /// @brief Virtual destructor.
    virtual ~IUniverseSelector() = default;
    /// @brief Return the universe selection for the current step.
    virtual UniverseSelection select() = 0;
};

/// @brief No-op selector returning an empty universe.
class NullUniverseSelector final : public IUniverseSelector {
public:
    UniverseSelection select() override { return UniverseSelection{}; }
};

} // namespace QTrading::Universe
