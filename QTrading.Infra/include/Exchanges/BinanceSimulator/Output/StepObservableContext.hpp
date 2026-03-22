#pragma once

#include <cstdint>
#include <memory>

#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

/// Step-scoped facts shared by output publishers/builders.
/// It currently centralizes market/step facts and reduces repeated reads, while
/// some snapshot fields (for example balances) are still read from runtime state.
struct StepObservableContext {
    /// Replay timestamp for the current step.
    uint64_t ts_exchange{ 0 };
    /// Monotonic successful-step sequence.
    uint64_t step_seq{ 0 };
    /// True when this context represents a termination step.
    bool replay_exhausted{ false };
    /// Market payload prepared by replay kernel for this step.
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> market_payload{};
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
