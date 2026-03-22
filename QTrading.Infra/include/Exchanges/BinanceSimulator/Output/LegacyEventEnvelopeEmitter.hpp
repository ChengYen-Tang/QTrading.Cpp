#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class LegacyEventEnvelopeEmitter final {
public:
    explicit LegacyEventEnvelopeEmitter(BinanceExchange& exchange) noexcept;

    void emit(BinanceExchange::EventEnvelope&& envelope) const;

private:
    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output

