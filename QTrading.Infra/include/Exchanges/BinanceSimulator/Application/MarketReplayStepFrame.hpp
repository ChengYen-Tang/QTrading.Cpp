#pragma once

#include <cstdint>
#include <memory>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Result of advancing replay cursors for one simulator step.
struct MarketReplayStepFrame {
    /// False when replay reached end-of-stream and no step payload exists.
    bool has_next{ false };
    /// Exchange timestamp chosen for the frame.
    uint64_t ts_exchange{ 0 };
    /// Aggregated market payload for all symbols active at `ts_exchange`.
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> market_payload;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
