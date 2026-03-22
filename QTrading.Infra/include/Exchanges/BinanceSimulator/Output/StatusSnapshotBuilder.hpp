#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class StatusSnapshotBuilder final {
public:
    static void Fill(const BinanceExchange& owner, Contracts::StatusSnapshot& out);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
