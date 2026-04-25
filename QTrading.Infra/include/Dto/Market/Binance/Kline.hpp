#pragma once

#include "Dto/Market/Binance/TradeKline.hpp"

namespace QTrading::Dto::Market::Binance {

    // Legacy compatibility alias. New code should use TradeKlineDto.
    using KlineDto = TradeKlineDto;

}  // namespace QTrading::Dto::Market::Binance
