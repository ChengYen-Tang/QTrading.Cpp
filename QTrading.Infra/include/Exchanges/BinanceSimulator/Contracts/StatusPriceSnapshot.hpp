#pragma once

#include <cstdint>
#include <string>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// Per-symbol price row embedded inside the public status snapshot.
struct StatusPriceSnapshot {
    /// Symbol name aligned with the replay symbol table.
    std::string symbol;
    /// Legacy-facing price field, currently populated from the best available close/mark source.
    double price{ 0.0 };
    /// True when `price` is populated.
    bool has_price{ false };
    /// Raw trade close price for the symbol.
    double trade_price{ 0.0 };
    /// True when a trade close price is available.
    bool has_trade_price{ false };
    /// Mark price exposed for the symbol.
    double mark_price{ 0.0 };
    /// True when a mark price is available.
    bool has_mark_price{ false };
    /// Encoded `ReferencePriceSource` for `mark_price`.
    int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
    /// Index price exposed for the symbol.
    double index_price{ 0.0 };
    /// True when an index price is available.
    bool has_index_price{ false };
    /// Encoded `ReferencePriceSource` for `index_price`.
    int32_t index_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
