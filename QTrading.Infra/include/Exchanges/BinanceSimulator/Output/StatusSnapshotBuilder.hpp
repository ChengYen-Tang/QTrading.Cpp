#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class BinanceExchange::StatusSnapshotBuilder final {
public:
    static void Fill(const BinanceExchange& owner, BinanceExchange::StatusSnapshot& out);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
