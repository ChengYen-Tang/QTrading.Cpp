#pragma once

namespace QTrading::Dto::Market {

    /// @brief Base type for market data DTOs.
    /// @details Carries a global timestamp (milliseconds since epoch) common to all market messages.
    struct BaseMarketDto {
        /// @brief Timestamp in milliseconds since UNIX epoch.
        unsigned long long Timestamp;
    };

}  // namespace QTrading::Dto::Market
