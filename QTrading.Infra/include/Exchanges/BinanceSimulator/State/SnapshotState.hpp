#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {

/// Id-based internal price row cache used by SnapshotBuilder hot path.
struct SnapshotPriceRowById {
    double price{ 0.0 };
    bool has_price{ false };
    double trade_price{ 0.0 };
    bool has_trade_price{ false };
    double mark_price{ 0.0 };
    bool has_mark_price{ false };
    int32_t mark_price_source{ static_cast<int32_t>(Contracts::ReferencePriceSource::None) };
    double index_price{ 0.0 };
    bool has_index_price{ false };
    int32_t index_price_source{ static_cast<int32_t>(Contracts::ReferencePriceSource::None) };
};

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
    /// Internal id-based cache used to materialize public `StatusSnapshot::prices`.
    std::vector<SnapshotPriceRowById> price_rows_by_symbol;
    /// Dirty flag per symbol id for incremental public row materialization.
    std::vector<uint8_t> price_row_dirty_by_symbol;
    /// Dirty symbol ids collected during latest snapshot-state update.
    std::vector<size_t> dirty_price_symbol_ids;
    /// Monotonic version bumped when any internal price row changes.
    uint64_t price_rows_version{ 0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::State
