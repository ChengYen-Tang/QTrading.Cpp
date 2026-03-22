#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

class BinanceExchange::EventCaptureBoundary final {
public:
    struct Input {
        QTrading::Infra::Logging::StepLogContext ctx{};
        std::vector<DomainMarketEvent> market_events;
        std::vector<DomainFundingEvent> funding_events;
        AccountSnapshotEvent account_snapshot{};
        PositionSnapshotEvent position_snapshot{};
        OrderSnapshotEvent order_snapshot{};
        std::vector<DomainFillEvent> fill_events;
        uint64_t cur_ver{ 0 };
    };

    static EventEnvelope Build(Input&& input)
    {
        EventEnvelope envelope{};
        envelope.ctx = input.ctx;
        envelope.market_events = std::move(input.market_events);
        envelope.funding_events = std::move(input.funding_events);
        envelope.account_snapshot = input.account_snapshot;
        envelope.position_snapshot = std::move(input.position_snapshot);
        envelope.order_snapshot = std::move(input.order_snapshot);
        envelope.account_events.reserve(input.fill_events.size());
        envelope.position_events.reserve(input.fill_events.size());
        envelope.order_events.reserve(input.fill_events.size());
        for (const auto& fill : input.fill_events) {
            envelope.account_events.push_back(AccountDomainEvent{ fill });
            envelope.position_events.push_back(PositionDomainEvent{ fill });
            envelope.order_events.push_back(OrderDomainEvent{ fill });
        }
        envelope.fill_events = std::move(input.fill_events);
        envelope.cur_ver = input.cur_ver;
        return envelope;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim
