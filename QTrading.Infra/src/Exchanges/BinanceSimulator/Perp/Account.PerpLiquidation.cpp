#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

void Account::update_unrealized_for_symbol_(const std::string& symbol, double mark_price)
{
    auto it_idx = position_indices_by_symbol_.find(symbol);
    if (it_idx != position_indices_by_symbol_.end()) {
        for (size_t idx : it_idx->second) {
            if (idx >= positions_.size()) continue;
            auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            pos.unrealized_pnl = (mark_price - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
        return;
    }
    for (auto& pos : positions_) {
        if (pos.symbol != symbol) continue;
        pos.unrealized_pnl = (mark_price - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
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
    if (!has_open_perp_position_()) {
        return;
    }

    constexpr double kWarningZoneRatio = 1.05;
    constexpr double kWarningReductionFraction = 0.15;
    constexpr int kMaxLiquidationStepsPerTick = 8;
    const auto in_warning_zone = [&](const QTrading::Dto::Account::BalanceSnapshot& s) -> bool {
        return (s.MaintenanceMargin > 0.0) &&
            (s.MarginBalance >= s.MaintenanceMargin) &&
            (s.MarginBalance < s.MaintenanceMargin * kWarningZoneRatio);
    };

    bool distressed = snapshot.MarginBalance < snapshot.MaintenanceMargin;
    bool warning_zone = in_warning_zone(snapshot);
    if (!warning_zone && !distressed) {
        return;
    }
    positions_changed = true;

    if (distressed) {
        std::cerr << "[update_positions] Liquidation triggered! marginBalance=" << snapshot.MarginBalance
            << ", maintenanceMargin=" << snapshot.MaintenanceMargin << "\n";
    }
    else {
        std::cerr << "[update_positions] Warning zone triggered: staged reduction. marginBalance=" << snapshot.MarginBalance
            << ", maintenanceMargin=" << snapshot.MaintenanceMargin << "\n";
    }

    // Phase 1: cancel all perp open orders first.
    if (distressed) {
        const size_t before = open_orders_.size();
        open_orders_.erase(
            std::remove_if(open_orders_.begin(), open_orders_.end(),
                [](const Order& o) { return o.instrument_type == InstrumentType::Perp; }),
            open_orders_.end());
        if (open_orders_.size() != before) {
            open_orders_changed = true;
            mark_open_orders_dirty_();
            rebuild_open_order_index_();
        }
        snapshot = get_perp_balance();
        distressed = snapshot.MarginBalance < snapshot.MaintenanceMargin;
        warning_zone = in_warning_zone(snapshot);
    }

    // Phase 2: staged partial liquidation.
    for (int step = 0; step < kMaxLiquidationStepsPerTick; ++step) {
        snapshot = get_perp_balance();
        if (!has_open_perp_position_()) break;
        const bool step_distressed = snapshot.MarginBalance < snapshot.MaintenanceMargin;
        if (!step_distressed) {
            if (!(warning_zone && step == 0)) {
                break;
            }
        }

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
        const TradeKlineDto* kptr = kline_by_id_[sym_id];
        if (!kptr) break;
        const auto market_ctx = build_symbol_market_context_(sym_id);

        const double liq_price = policies_.liquidation_price_ctx
            ? policies_.liquidation_price_ctx(pos, market_ctx)
            : get_last_mark_price_(pos.symbol);
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

        double staged_fraction = kWarningReductionFraction;
        if (step_distressed) {
            const double deficit_ratio = deficit / std::max(1e-12, snapshot.MaintenanceMargin);
            if (deficit_ratio < 0.10) {
                staged_fraction = 0.25;
            }
            else if (deficit_ratio < 0.30) {
                staged_fraction = 0.50;
            }
            else {
                staged_fraction = 1.0;
            }
        }

        double desired_close = std::clamp(pos.quantity * staged_fraction, 1e-8, pos.quantity);
        if (step_distressed && deficit > 0.0 && denom > 1e-12) {
            desired_close = std::min(pos.quantity, std::max(desired_close, deficit / denom));
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
        const double mark = get_last_mark_price_(fill.symbol);
        if (mark > 0.0) {
            update_unrealized_for_symbol_(fill.symbol, mark);
        }
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
            const double mark = get_last_mark_price_(p.symbol);
            if (mark <= 0.0) {
                continue;
            }
            p.unrealized_pnl = (mark - p.entry_price) * p.quantity * (p.is_long ? 1.0 : -1.0);
        }
    }

    // Phase 3: unresolved residual handling (no ADL/insurance modeling yet).
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
                rebuild_open_order_index_();
            }
        }
        order_to_position_.clear();
        for (const auto& p : positions_) {
            if (p.order_id > 0) {
                order_to_position_[p.order_id] = p.id;
            }
        }
    }
}
