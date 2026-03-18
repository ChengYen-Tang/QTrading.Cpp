#pragma once

#include <cstdint>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/StepCompareModel.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

class BinanceCompareBridge final {
public:
    static StepStateComparePayload FromStepCompareSnapshot(
        const BinanceExchange::StepCompareSnapshot& snapshot,
        ReplayCompareStatus status = ReplayCompareStatus::Success,
        bool fallback_to_legacy = false);

    static ReplayEventSummary FromAsyncOrderAck(
        const BinanceExchange::AsyncOrderAck& ack,
        uint64_t ts_exchange);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
