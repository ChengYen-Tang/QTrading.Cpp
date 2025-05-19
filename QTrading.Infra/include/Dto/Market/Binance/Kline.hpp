#pragma once

#include <chrono>
#include "Dto/Market/Base.hpp"

namespace QTrading::Dto::Market::Binance {

    using std::chrono::system_clock;

    /// @brief Data Transfer Object for a single Binance kline (candlestick/bar).
    /// @details Inherits BaseMarketDto for the open timestamp; includes OHLC, volume,
    ///          trade count, and both raw and chrono timestamps.
    struct KlineDto : BaseMarketDto {
        /// @brief Open price of the interval.
        double OpenPrice;

        /// @brief Highest price reached.
        double HighPrice;

        /// @brief Lowest price reached.
        double LowPrice;

        /// @brief Close price of the interval.
        double ClosePrice;

        /// @brief Traded base-asset volume.
        double Volume;

        /// @brief Close time as milliseconds since epoch.
        unsigned long long CloseTime;

        /// @brief Close time as a std::chrono::system_clock::time_point.
        system_clock::time_point CloseDateTime;

        /// @brief Traded quote-asset volume.
        double QuoteVolume;

        /// @brief Number of trades in this interval.
        int TradeCount;

        /// @brief Base-asset volume bought by takers.
        double TakerBuyBaseVolume;

        /// @brief Quote-asset volume paid by takers.
        double TakerBuyQuoteVolume;

        /// @brief Constructs a fully-populated KlineDto.
        /// @param openTs     Open timestamp (ms since epoch) → sets BaseMarketDto::Timestamp.
        /// @param openP      Open price.
        /// @param highP      High price.
        /// @param lowP       Low price.
        /// @param closeP     Close price.
        /// @param vol        Base-asset volume.
        /// @param closeTs    Close timestamp (ms since epoch).
        /// @param quoteVol   Quote-asset volume.
        /// @param tradeCnt   Number of trades.
        /// @param takerBB    Taker buy base volume.
        /// @param takerBQ    Taker buy quote volume.
        KlineDto(unsigned long long openTs,
            double openP,
            double highP,
            double lowP,
            double closeP,
            double vol,
            unsigned long long closeTs,
            double quoteVol,
            int tradeCnt,
            double takerBB,
            double takerBQ)
            : OpenPrice(openP),
            HighPrice(highP),
            LowPrice(lowP),
            ClosePrice(closeP),
            Volume(vol),
            CloseTime(closeTs),
            CloseDateTime(system_clock::time_point{ std::chrono::milliseconds(closeTs) }),
            QuoteVolume(quoteVol),
            TradeCount(tradeCnt),
            TakerBuyBaseVolume(takerBB),
            TakerBuyQuoteVolume(takerBQ)
        {
            Timestamp = openTs;
        }

        /// @brief Default constructor (all fields zero-initialized).
        KlineDto() = default;
    };

}  // namespace QTrading::Dto::Market::Binance
