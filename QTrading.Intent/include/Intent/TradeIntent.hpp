#pragma once

#include "Contracts/StrategyIdentity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace QTrading::Intent {

/// @brief Directional intent for a trade leg.
enum class TradeSide {
    Long,
    Short
};

/// @brief One leg of a multi-leg trade intent.
struct TradeLeg {
    /// @brief Instrument identifier (symbol, product, etc.).
    std::string instrument;
    /// @brief Directional intent.
    TradeSide side = TradeSide::Long;
};

/// @brief Strategy intent describing structure and legs.
struct TradeIntent {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Unique intent identifier.
    std::string intent_id;
    /// @brief Strategy identifier.
    std::string strategy;
    /// @brief Typed strategy identity for downstream policy routing.
    QTrading::Contracts::StrategyKind strategy_kind = QTrading::Contracts::StrategyKind::Unknown;
    /// @brief Structure name (e.g., delta_neutral_carry).
    std::string structure;
    /// @brief Typed trade structure for downstream policy routing.
    QTrading::Contracts::TradeStructureKind structure_kind = QTrading::Contracts::TradeStructureKind::Unknown;
    /// @brief Position mode (hedge/oneway).
    std::string position_mode;
    /// @brief Urgency hint.
    std::string urgency;
    /// @brief Strategy confidence passed from signal (0..1).
    ///        Risk engine may use this to scale target size dynamically.
    double confidence = 1.0;
    /// @brief Free-form reason or context.
    std::string reason;
    /// @brief Ordered legs for the structure.
    std::vector<TradeLeg> legs;
};

} // namespace QTrading::Intent
