#pragma once

#include <cstdint>
#include <memory>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

class MarketReplayKernel final {
public:
    struct StepFrame {
        bool has_next{ false };
        uint64_t ts_exchange{ 0 };
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> market_payload;
    };

    static StepFrame Next(State::StepKernelState& state);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
