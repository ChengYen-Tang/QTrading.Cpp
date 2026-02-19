#pragma once

#include <utility>
#include <vector>
#include "IUniverseSelector.hpp"

namespace QTrading::Universe {

/// @brief Fixed universe selector returning a static symbol list.
class FixedUniverseSelector final : public IUniverseSelector {
public:
    /// @brief Construct with a fixed universe list.
    explicit FixedUniverseSelector(std::vector<std::string> symbols = {});

    /// @brief Return the fixed universe selection.
    UniverseSelection select() override;

private:
    std::vector<std::string> symbols_;
};

} // namespace QTrading::Universe
