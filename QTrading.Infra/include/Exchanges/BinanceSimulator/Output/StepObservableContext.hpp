#pragma once

#include <cstdint>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

struct StepObservableContext {
    uint64_t ts_exchange{ 0 };
    uint64_t step_seq{ 0 };
    bool replay_exhausted{ false };
    QTrading::Dto::Market::Binance::MultiKlineDto* market_payload{ nullptr };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
