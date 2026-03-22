#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class EventEnvelopePublisher final {
public:
    explicit EventEnvelopePublisher(BinanceExchange& exchange) noexcept;

    void publish(BinanceExchange::EventEnvelope&& task) const;

private:
    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output

