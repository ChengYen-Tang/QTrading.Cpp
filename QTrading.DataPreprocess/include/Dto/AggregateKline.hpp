#pragma once

#include <memory>
#include <unordered_map>
#include <deque>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Market/Binance/Kline.hpp"

namespace QTrading::DataPreprocess::Dto {

    struct AggregateKline {
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> CurrentKlines;

        std::unordered_map<std::string,
            std::deque<QTrading::Dto::Market::Binance::KlineDto>> HistoricalKlines;
    };
}