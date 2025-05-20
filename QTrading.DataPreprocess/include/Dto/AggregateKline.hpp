#pragma once

#include <memory>
#include <unordered_map>
#include <deque>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Market/Binance/Kline.hpp"

namespace QTrading::DataPreprocess::Dto {

    /// @brief  Container for the current minute‐level data and historical hourly aggregates.
    struct AggregateKline {
        /// @brief  Shared pointer to the latest MultiKlineDto containing this minute's bars.
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> CurrentKlines;

        /// @brief  Map from symbol to deque of finished hourly KlineDto bars.
        std::unordered_map<std::string,
            std::deque<QTrading::Dto::Market::Binance::KlineDto>> HistoricalKlines;
    };
}
