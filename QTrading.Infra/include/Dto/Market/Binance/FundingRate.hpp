#pragma once

#include <cstdint>
#include <optional>
#include "Dto/Market/Base.hpp"

namespace QTrading::Dto::Market::Binance {

    /// @brief DTO for a single funding rate event.
    /// @details FundingTime is milliseconds since epoch; MarkPrice may be missing.
    struct FundingRateDto : QTrading::Dto::Market::BaseMarketDto {
        /// @brief The time the funding rate is applied (ms since epoch).
        uint64_t FundingTime{};

        /// @brief Funding rate for the given symbol and time.
        double Rate{};

        /// @brief Mark price (optional).
        std::optional<double> MarkPrice;

        FundingRateDto() = default;

        FundingRateDto(uint64_t fundingTime, double rate, std::optional<double> markPrice = std::nullopt)
            : FundingTime(fundingTime), Rate(rate), MarkPrice(markPrice)
        {
            Timestamp = fundingTime;
        }
    };

}  // namespace QTrading::Dto::Market::Binance
