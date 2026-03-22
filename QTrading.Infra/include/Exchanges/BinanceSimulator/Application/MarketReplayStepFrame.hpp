#pragma once

#include <cstdint>
#include <memory>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

struct MarketReplayStepFrame {
    bool has_next{ false };
    uint64_t ts_exchange{ 0 };
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> market_payload;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
