#pragma once

#include <cstdint>

namespace QTrading::Dto::Market::Binance {

    // Minimal kline shape used by mark/index reference streams.
    struct ReferenceKlineDto {
        uint64_t OpenTime{ 0 };
        double OpenPrice{ 0.0 };
        double HighPrice{ 0.0 };
        double LowPrice{ 0.0 };
        double ClosePrice{ 0.0 };
        uint64_t CloseTime{ 0 };

        static ReferenceKlineDto Point(uint64_t ts, double price)
        {
            return ReferenceKlineDto{
                ts,
                price,
                price,
                price,
                price,
                ts
            };
        }
    };

}  // namespace QTrading::Dto::Market::Binance

