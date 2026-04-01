#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Read-model cache consumed by SnapshotBuilder.
/// It caches replay/price context from successful steps to reduce, but not
/// fully eliminate, on-demand snapshot rebuild work.
struct SnapshotState {
    /// Last successful replay timestamp and sequence.
    uint64_t ts_exchange{ 0 };
    uint64_t step_seq{ 0 };
    /// Replay progress percentage in [0, 100].
    double progress_pct{ 0.0 };
    /// Shared symbol table for price snapshots.
    std::shared_ptr<const std::vector<std::string>> symbols_shared;
    /// Latest trade close price observed per symbol.
    std::vector<double> last_trade_price_by_symbol;
    /// Availability flags paired with the cached trade prices.
    std::vector<uint8_t> has_last_trade_price_by_symbol;
    /// Latest mark price observed per symbol.
    std::vector<double> last_mark_price_by_symbol;
    /// Availability flags paired with the cached mark prices.
    std::vector<uint8_t> has_last_mark_price_by_symbol;
    /// Exchange timestamp of the cached mark price per symbol.
    std::vector<uint64_t> last_mark_price_ts_by_symbol;
    /// Encoded `ReferencePriceSource` for the cached mark price per symbol.
    std::vector<int32_t> last_mark_price_source_by_symbol;
    /// Latest index price observed per symbol.
    std::vector<double> last_index_price_by_symbol;
    /// Availability flags paired with the cached index prices.
    std::vector<uint8_t> has_last_index_price_by_symbol;
    /// Exchange timestamp of the cached index price per symbol.
    std::vector<uint64_t> last_index_price_ts_by_symbol;
    /// Encoded `ReferencePriceSource` for the cached index price per symbol.
    std::vector<int32_t> last_index_price_source_by_symbol;
    /// Last full market payload, kept for future output extensions.
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> last_market_payload;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
