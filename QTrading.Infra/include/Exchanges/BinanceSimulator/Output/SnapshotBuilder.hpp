#pragma once

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

/// Builds externally visible status snapshots from stable read-model state.
/// Intended read path for FillStatusSnapshot() in rebuilt skeleton.
class SnapshotBuilder final {
public:
    /// Fills `out` with the latest snapshot view without mutating exchange state.
    static void Fill(const BinanceExchange& exchange, Contracts::StatusSnapshot& out);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
