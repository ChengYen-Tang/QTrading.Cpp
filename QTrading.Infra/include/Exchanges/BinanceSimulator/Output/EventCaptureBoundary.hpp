#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/EventCaptureInput.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class EventCaptureBoundary final {
public:
    static BinanceExchange::EventEnvelope Build(EventCaptureInput&& input)
    {
        BinanceExchange::EventEnvelope envelope{};
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

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
