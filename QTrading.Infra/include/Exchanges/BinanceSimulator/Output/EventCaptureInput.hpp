#pragma once

#include <cstdint>
#include <vector>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

struct EventCaptureInput {
    QTrading::Infra::Logging::StepLogContext ctx{};
    std::vector<DomainMarketEvent> market_events;
    std::vector<DomainFundingEvent> funding_events;
    AccountSnapshotEvent account_snapshot{};
    PositionSnapshotEvent position_snapshot{};
    OrderSnapshotEvent order_snapshot{};
    std::vector<DomainFillEvent> fill_events;
    uint64_t cur_ver{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
