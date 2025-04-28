#pragma once

#include <unordered_map>
#include <optional>
#include <string>
#include "Dto/Market/Base.hpp"
#include "Dto/Market/Binance/Kline.hpp"

namespace QTrading::Dto::Market::Binance {
    struct MultiKlineDto : QTrading::Dto::Market::BaseMarketDto {
        // key = symbol, value = std::nullopt if there is no data for this minute
        std::unordered_map<std::string, std::optional<KlineDto>> klines;
    };
}
