#pragma once

#include <cstdint>
#include <string>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct StatusPriceSnapshot {
    std::string symbol;
    double price{ 0.0 };
    bool has_price{ false };
    double trade_price{ 0.0 };
    bool has_trade_price{ false };
    double mark_price{ 0.0 };
    bool has_mark_price{ false };
    int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
    double index_price{ 0.0 };
    bool has_index_price{ false };
    int32_t index_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
