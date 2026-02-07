#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

void Account::update_unrealized_for_symbol_(const std::string& symbol, double close_price)
{
    auto it_idx = position_indices_by_symbol_.find(symbol);
    if (it_idx != position_indices_by_symbol_.end()) {
        for (size_t idx : it_idx->second) {
            if (idx >= positions_.size()) continue;
            auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            pos.unrealized_pnl = (close_price - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
        return;
    }
    for (auto& pos : positions_) {
        if (pos.symbol != symbol) continue;
        pos.unrealized_pnl = (close_price - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
    }
}

bool Account::has_open_perp_position_() const
{
    for (const auto& p : positions_) {
        if (p.instrument_type == InstrumentType::Perp && p.quantity > 1e-8) {
            return true;
        }
    }
    return false;
}

void Account::apply_perp_liquidation_(double taker_fee, bool& open_orders_changed, bool& positions_changed)
{
    auto snapshot = get_perp_balance();
    if (snapshot.MarginBalance >= snapshot.MaintenanceMargin || !has_open_perp_position_()) {
        return;
    }

    positions_changed = true;
    constexpr int kMaxLiquidationStepsPerTick = 8;

    std::cerr << "[update_positions] Liquidation triggered! marginBalance=" << snapshot.MarginBalance
        << ", maintenanceMargin=" << snapshot.MaintenanceMargin << "\n";

    const size_t open_before = open_orders_.size();
    open_orders_.erase(
        std::remove_if(open_orders_.begin(), open_orders_.end(),
            [](const Order& o) {
                return o.instrument_type == InstrumentType::Perp && o.closing_position_id < 0;
            }),
        open_orders_.end());
    if (open_orders_.size() != open_before) {
        open_orders_changed = true;
        mark_open_orders_dirty_();
    }
    rebuild_open_order_index_();

    for (int step = 0; step < kMaxLiquidationStepsPerTick; ++step) {
        snapshot = get_perp_balance();
        if (!has_open_perp_position_()) break;
        if (snapshot.MarginBalance >= snapshot.MaintenanceMargin) break;

        int worst_idx = -1;
        double worst_unreal = 0.0;
        for (int i = 0; i < static_cast<int>(positions_.size()); ++i) {
            if (positions_[i].instrument_type != InstrumentType::Perp) {
                continue;
            }
            if (worst_idx < 0 || positions_[i].unrealized_pnl < worst_unreal) {
                worst_unreal = positions_[i].unrealized_pnl;
                worst_idx = i;
            }
        }
        if (worst_idx < 0) break;

        Position& pos = positions_[worst_idx];
        const size_t sym_id = get_symbol_id_(pos.symbol);
        if (sym_id >= kline_by_id_.size()) break;
        const KlineDto* kptr = kline_by_id_[sym_id];
        if (!kptr) break;
        const KlineDto& k = *kptr;

        const double liq_price = policies_.liquidation_price
            ? policies_.liquidation_price(pos, k)
            : (pos.is_long ? k.LowPrice : k.HighPrice);
        if (liq_price <= 0.0) break;

        double vol_avail = 0.0;
        if (sym_id < remaining_vol_.size()) {
            vol_avail = remaining_vol_[sym_id];
        }
        if (vol_avail <= 1e-8) break;

        const double dir = pos.is_long ? 1.0 : -1.0;
        const double pnl_per_unit = (liq_price - pos.entry_price) * dir;
        const double fee_per_unit = (liq_price * taker_fee);
        const double maint_per_unit = (pos.quantity > 1e-12) ? (pos.maintenance_margin / pos.quantity) : 0.0;

        const double deficit = snapshot.MaintenanceMargin - snapshot.MarginBalance;
        const double denom = (pnl_per_unit - fee_per_unit + maint_per_unit);

        double desired_close = pos.quantity;
        if (deficit > 0.0 && denom > 1e-12) {
            desired_close = std::min(pos.quantity, deficit / denom);
        }

        desired_close = std::clamp(desired_close, 1e-8, pos.quantity);

        const double close_qty = std::min({ pos.quantity, vol_avail, desired_close });
        if (close_qty <= 1e-8) break;

        if (sym_id < remaining_vol_.size()) {
            remaining_vol_[sym_id] = vol_avail - close_qty;
        }

        const OrderSide liq_side = pos.is_long ? OrderSide::Sell : OrderSide::Buy;
        Order liq_ord{
            -999999,
            pos.symbol,
            close_qty,
            liq_price,
            liq_side,
            PositionSide::Both,
            false,
            pos.id
        };
        liq_ord.instrument_type = pos.instrument_type;

        const double notional = close_qty * liq_price;
        const double fee = notional * taker_fee;
        std::vector<Order> leftover;
        leftover.reserve(1);
        processClosingOrder(liq_ord, close_qty, liq_price, fee, leftover);

        FillEvent fill{};
        fill.order_id = liq_ord.id;
        fill.symbol = liq_ord.symbol;
        fill.side = liq_ord.side;
        fill.position_side = liq_ord.position_side;
        fill.reduce_only = liq_ord.reduce_only;
        fill.order_qty = liq_ord.quantity;
        fill.order_price = liq_ord.price;
        fill.exec_qty = close_qty;
        fill.exec_price = liq_price;
        fill.remaining_qty = 0.0;
        fill.is_taker = true;
        fill.fee = fee;
        fill.fee_rate = taker_fee;
        fill.closing_position_id = liq_ord.closing_position_id;
        fill.instrument_type = liq_ord.instrument_type;
        update_unrealized_for_symbol_(fill.symbol, kptr->ClosePrice);
        mark_balance_dirty_();
        fill.perp_balance_snapshot = get_perp_balance();
        fill.spot_balance_snapshot = get_spot_balance();
        fill.total_cash_balance_snapshot = get_total_cash_balance();
        fill.balance_snapshot = fill.perp_balance_snapshot;
        fill.positions_snapshot = positions_;
        fill_events_.push_back(std::move(fill));

        positions_.erase(
            std::remove_if(positions_.begin(), positions_.end(),
                [](const Position& p) { return p.quantity <= 1e-8; }),
            positions_.end());

        merge_positions();
        rebuild_position_index_();

        for (auto& p : positions_) {
            const size_t pid = get_symbol_id_(p.symbol);
            if (pid >= kline_by_id_.size()) {
                continue;
            }
            const KlineDto* pk = kline_by_id_[pid];
            if (!pk) {
                continue;
            }
            const double cp = pk->ClosePrice;
            p.unrealized_pnl = (cp - p.entry_price) * p.quantity * (p.is_long ? 1.0 : -1.0);
        }
    }

    snapshot = get_perp_balance();
    if (has_open_perp_position_() && snapshot.MarginBalance < snapshot.MaintenanceMargin) {
        std::cerr << "[update_positions] Liquidation unresolved after steps, forcing account bankruptcy\n";
        perp_ledger_.reset_bankruptcy();
        positions_.erase(
            std::remove_if(positions_.begin(), positions_.end(),
                [](const Position& p) { return p.instrument_type == InstrumentType::Perp; }),
            positions_.end());
        mark_balance_dirty_();
        rebuild_position_index_();
        if (!open_orders_.empty()) {
            const auto before = open_orders_.size();
            open_orders_.erase(
                std::remove_if(open_orders_.begin(), open_orders_.end(),
                    [](const Order& o) { return o.instrument_type == InstrumentType::Perp; }),
                open_orders_.end());
            if (open_orders_.size() != before) {
                open_orders_changed = true;
                mark_open_orders_dirty_();
            }
            rebuild_open_order_index_();
        }
        order_to_position_.clear();
        for (const auto& p : positions_) {
            if (p.order_id > 0) {
                order_to_position_[p.order_id] = p.id;
            }
        }
    }
}
