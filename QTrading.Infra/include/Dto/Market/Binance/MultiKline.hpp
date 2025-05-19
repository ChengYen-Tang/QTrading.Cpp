#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include "Dto/Market/Base.hpp"
#include "Dto/Market/Binance/Kline.hpp"

namespace QTrading::Dto::Market::Binance {

    /// @brief DTO containing multiple symbols’ 1-minute klines at a given timestamp.
    /// @details Map key = trading symbol; map value = optional KlineDto (nullopt if no data this minute).
    struct MultiKlineDto : QTrading::Dto::Market::BaseMarketDto {
        /// @brief Per-symbol minute bar data; std::nullopt if missing for that symbol.
        std::unordered_map<std::string, std::optional<KlineDto>> klines;
    };

}  // namespace QTrading::Dto::Market::Binance
