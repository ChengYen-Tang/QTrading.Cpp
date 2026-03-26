#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

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
    /// Full position snapshot for this step (independent from channel gate).
    const std::vector<QTrading::dto::Position>* position_snapshot{ nullptr };
    /// Full order snapshot for this step (independent from channel gate).
    const std::vector<QTrading::dto::Order>* order_snapshot{ nullptr };
    /// Monotonic state version used by channel publish gate.
    uint64_t account_state_version{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
