#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

class BinanceExchange::PositionOrderSnapshotGate final {
public:
    struct ChangeDecision {
        bool version_changed{ false };
        bool pos_changed{ false };
        bool ord_changed{ false };
    };

    static ChangeDecision EvaluateAndPublish(BinanceExchange& owner,
        uint64_t cur_ver,
        const PositionSnapshotPtr& cur_positions,
        const OrderSnapshotPtr& cur_orders,
        SideEffectStepSnapshot& side_effect_snapshot);

    static void CommitSnapshotsAndVersion(BinanceExchange& owner,
        uint64_t cur_ver,
        const PositionSnapshotPtr& cur_positions,
        const OrderSnapshotPtr& cur_orders,
        const ChangeDecision& decision);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim
