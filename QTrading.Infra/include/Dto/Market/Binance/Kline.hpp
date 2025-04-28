#pragma once
#include <chrono>  
#include "Dto/Market/Base.hpp"

using namespace QTrading::Dto::Market;
using namespace std::chrono;

namespace QTrading::Dto::Market::Binance {
    struct KlineDto : BaseMarketDto {
        double OpenPrice;
        double HighPrice;
        double LowPrice;
        double ClosePrice;
        double Volume;
        unsigned long long CloseTime;
        system_clock::time_point CloseDateTime;
        double QuoteVolume;
        int TradeCount;
        double TakerBuyBaseVolume;
        double TakerBuyQuoteVolume;

        KlineDto(unsigned long long openTs,
            double openP, double highP, double lowP, double closeP,
            double vol, unsigned long long closeTs,
            double quoteVol, int tradeCnt,
            double takerBuyBase, double takerBuyQuote)
            : OpenPrice(openP),
            HighPrice(highP),
            LowPrice(lowP),
            ClosePrice(closeP),
            Volume(vol),
            CloseTime(closeTs),
            CloseDateTime(system_clock::time_point{ milliseconds(closeTs) }),
            QuoteVolume(quoteVol),
            TradeCount(tradeCnt),
            TakerBuyBaseVolume(takerBuyBase),
            TakerBuyQuoteVolume(takerBuyQuote)
        {
            Timestamp = openTs;
        }

        KlineDto() = default;
    };
}