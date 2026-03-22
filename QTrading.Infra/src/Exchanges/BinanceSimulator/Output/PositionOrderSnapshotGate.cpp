#include "Exchanges/BinanceSimulator/Output/PositionOrderSnapshotGate.hpp"

#include "Diagnostics/Trace.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

BinanceExchange::PositionOrderSnapshotGate::ChangeDecision
BinanceExchange::PositionOrderSnapshotGate::EvaluateAndPublish(BinanceExchange& owner,
    uint64_t cur_ver,
    const PositionSnapshotPtr& cur_positions,
    const OrderSnapshotPtr& cur_orders,
    SideEffectStepSnapshot& side_effect_snapshot)
{
    ChangeDecision decision{};
    decision.version_changed = (cur_ver != owner.last_account_version_);
    if (!decision.version_changed || !cur_positions || !cur_orders) {
        return decision;
    }

    const bool pos_unchanged = owner.last_pos_snapshot_
        ? BinanceExchange::vec_equal(*cur_positions, *owner.last_pos_snapshot_)
        : cur_positions->empty();
    const bool ord_unchanged = owner.last_ord_snapshot_
        ? BinanceExchange::vec_equal(*cur_orders, *owner.last_ord_snapshot_)
        : cur_orders->empty();

    if (!pos_unchanged) {
        QTR_TRACE("ex", "position_channel Send");
        if (owner.position_channel_publisher_) {
            owner.position_channel_publisher_(*cur_positions);
            side_effect_snapshot.position_published = true;
        }
        else if (owner.position_channel) {
            owner.position_channel->Send(*cur_positions);
            side_effect_snapshot.position_published = true;
        }
        decision.pos_changed = true;
    }

    if (!ord_unchanged) {
        QTR_TRACE("ex", "order_channel Send");
        if (owner.order_channel_publisher_) {
            owner.order_channel_publisher_(*cur_orders);
            side_effect_snapshot.order_published = true;
        }
        else if (owner.order_channel) {
            owner.order_channel->Send(*cur_orders);
            side_effect_snapshot.order_published = true;
        }
        decision.ord_changed = true;
    }

    return decision;
}

void BinanceExchange::PositionOrderSnapshotGate::CommitSnapshotsAndVersion(
    BinanceExchange& owner,
    uint64_t cur_ver,
    const PositionSnapshotPtr& cur_positions,
    const OrderSnapshotPtr& cur_orders,
    const ChangeDecision& decision)
{
    if (!decision.version_changed) {
        return;
    }
    if (decision.pos_changed) {
        owner.last_pos_snapshot_ = cur_positions;
    }
    if (decision.ord_changed) {
        owner.last_ord_snapshot_ = cur_orders;
    }
    owner.last_account_version_ = cur_ver;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
