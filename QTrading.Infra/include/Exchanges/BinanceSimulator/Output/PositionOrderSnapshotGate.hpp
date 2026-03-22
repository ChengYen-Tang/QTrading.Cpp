#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/PositionOrderSnapshotChangeDecision.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class PositionOrderSnapshotGate final {
public:
    static PositionOrderSnapshotChangeDecision EvaluateAndPublish(BinanceExchange& owner,
        uint64_t cur_ver,
        const BinanceExchange::PositionSnapshotPtr& cur_positions,
        const BinanceExchange::OrderSnapshotPtr& cur_orders,
        BinanceExchange::SideEffectStepSnapshot& side_effect_snapshot);

    static void CommitSnapshotsAndVersion(BinanceExchange& owner,
        uint64_t cur_ver,
        const BinanceExchange::PositionSnapshotPtr& cur_positions,
        const BinanceExchange::OrderSnapshotPtr& cur_orders,
        const PositionOrderSnapshotChangeDecision& decision);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
