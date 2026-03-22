#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

struct PositionOrderSnapshotChangeDecision {
    bool version_changed{ false };
    bool pos_changed{ false };
    bool ord_changed{ false };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
