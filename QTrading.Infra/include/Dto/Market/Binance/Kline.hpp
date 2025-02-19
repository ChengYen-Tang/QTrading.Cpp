#pragma once
#include "Dto/Market/Base.hpp"

using namespace QTrading::Dto::Market;

namespace QTrading::Dto::Market::Binance {
    struct KlineDto : BaseMarketDto {
        double OpenPrice;
        double HighPrice;
        double LowPrice;
        double ClosePrice;
        double Volume;
        unsigned long long CloseTime;
        double QuoteVolume;
        int TradeCount;
        double TakerBuyBaseVolume;
        double TakerBuyQuoteVolume;
    };
}