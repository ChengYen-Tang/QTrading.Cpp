#include "Exchanges/BinanceSimulator/Account/Account.hpp"

// This method is declared on Account but must be defined out-of-line.

void Account::processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover)
{
    if (ord.reduce_only) {
        if (!processReduceOnlyOrder(ord, fill_qty, fill_price, fee, leftover)) {
            // If no matching position to reduce, ignore the order.
        }
    }
    else {
        processNormalOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
    }
}

void Account::applyOpeningFillToPosition(Order& ord, double fill_qty, double fill_price, double notional,
    double init_margin, double maint_margin, double fee, double lev, double feeRate, bool is_spot_symbol)
{
    auto pm = order_to_position_.find(ord.id);
    if (pm == order_to_position_.end()) {
        const int pid = generate_position_id();
        const bool pos_is_long = hedge_mode_
            ? (ord.position_side == QTrading::Dto::Trading::PositionSide::Long)
            : (ord.side == QTrading::Dto::Trading::OrderSide::Buy);
        positions_.push_back(Position{
            pid,
            ord.id,
            ord.symbol,
            fill_qty,
            fill_price,
            pos_is_long,
            0.0,
            notional,
            is_spot_symbol ? 0.0 : init_margin,
            maint_margin,
            fee,
            lev,
            feeRate,
            ord.instrument_type
            });
        order_to_position_[ord.id] = pid;
        rebuild_position_index_();
        return;
    }

    const int pid = pm->second;
    auto itPosIdx = position_index_by_id_.find(pid);
    if (itPosIdx != position_index_by_id_.end()) {
        Position& pos = positions_[itPosIdx->second];
        const double old_notional = pos.notional;
        const double new_notional = old_notional + notional;
        const double old_qty = pos.quantity;
        const double new_qty = old_qty + fill_qty;
        const double new_entry = new_notional / new_qty;

        pos.quantity = new_qty;
        pos.entry_price = new_entry;
        pos.notional += notional;
        pos.initial_margin += is_spot_symbol ? 0.0 : init_margin;
        pos.maintenance_margin += maint_margin;
        pos.fee += fee;
        pos.instrument_type = ord.instrument_type;
        return;
    }

    // Fallback: index out of sync, do linear search.
    for (auto& pos : positions_) {
        if (pos.id == pid) {
            const double old_notional = pos.notional;
            const double new_notional = old_notional + notional;
            const double old_qty = pos.quantity;
            const double new_qty = old_qty + fill_qty;
            const double new_entry = new_notional / new_qty;

            pos.quantity = new_qty;
            pos.entry_price = new_entry;
            pos.notional += notional;
            pos.initial_margin += is_spot_symbol ? 0.0 : init_margin;
            pos.maintenance_margin += maint_margin;
            pos.fee += fee;
            pos.instrument_type = ord.instrument_type;
            return;
        }
    }
}
