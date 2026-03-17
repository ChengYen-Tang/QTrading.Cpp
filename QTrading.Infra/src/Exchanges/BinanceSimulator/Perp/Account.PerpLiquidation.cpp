#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

void Account::RiskEngine::RevalueAllUnrealized(Account& owner)
{
    for (auto& pos : owner.positions_) {
        const double mark = owner.get_last_mark_price_(pos.symbol);
        if (mark <= 0.0) {
            continue;
        }
        pos.unrealized_pnl = (mark - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
    }
}

void Account::RiskEngine::RefreshMaintenanceMargins(Account& owner)
{
    for (auto& p : owner.positions_) {
        const double mark = owner.get_last_mark_price_(p.symbol);
        if (mark <= 0.0) {
            continue;
        }
        p.notional = std::abs(p.quantity * mark);
        const auto& instrument_spec = owner.resolve_instrument_spec_(p.symbol);
        if (!instrument_spec.maintenance_margin_enabled) {
            p.maintenance_margin = 0.0;
            continue;
        }
        p.maintenance_margin = owner.maintenance_margin_for_notional_(p.notional);
    }
}

void Account::RiskEngine::UpdateUnrealizedForSymbol(Account& owner, const std::string& symbol, double mark_price)
{
    owner.update_unrealized_for_symbol_(symbol, mark_price);
}

void Account::RiskEngine::ApplyPerpLiquidation(Account& owner,
    double taker_fee,
    bool& open_orders_changed,
    bool& positions_changed)
{
    owner.apply_perp_liquidation_(taker_fee, open_orders_changed, positions_changed);
}

void Account::FillEventCollector::CaptureLiquidationFill(Account& owner,
    const Order& liq_order,
    const LiquidationFillInput& in)
{
    FillEvent fill{};
    fill.order_id = liq_order.id;
    fill.symbol = liq_order.symbol;
    fill.side = liq_order.side;
    fill.position_side = liq_order.position_side;
    fill.reduce_only = liq_order.reduce_only;
    fill.close_position = liq_order.close_position;
    fill.quote_order_qty = liq_order.quote_order_qty;
    fill.order_qty = liq_order.quantity;
    fill.order_price = liq_order.price;
    fill.exec_qty = in.close_qty;
    fill.exec_price = in.liq_price;
    fill.remaining_qty = 0.0;
    fill.is_taker = true;
    fill.fee = in.fee;
    fill.fee_rate = in.fee_rate;
    fill.closing_position_id = liq_order.closing_position_id;
    fill.instrument_type = liq_order.instrument_type;
    const double mark = owner.get_last_mark_price_(fill.symbol);
    if (mark > 0.0) {
        RiskEngine::UpdateUnrealizedForSymbol(owner, fill.symbol, mark);
    }
    owner.mark_balance_dirty_();
    fill.perp_balance_snapshot = owner.get_perp_balance();
    fill.spot_balance_snapshot = owner.get_spot_balance();
    fill.total_cash_balance_snapshot = owner.get_total_cash_balance();
    fill.balance_snapshot = fill.perp_balance_snapshot;
    fill.positions_snapshot = owner.positions_;
    owner.fill_events_.push_back(std::move(fill));
}

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
        const bool removed = filter_open_orders_([](const Order& o) {
            return o.instrument_type == InstrumentType::Perp;
            });
        if (removed) {
            open_orders_changed = true;
            mark_open_orders_dirty_();
        }
        snapshot = get_perp_balance();
        distressed = snapshot.MarginBalance < snapshot.MaintenanceMargin;
        warning_zone = in_warning_zone(snapshot);
    }

    // Phase 2: staged partial liquidation.
    std::vector<Order> liquidation_leftover;
    liquidation_leftover.reserve(1);
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
        liquidation_leftover.clear();
        processClosingOrder(liq_ord, close_qty, liq_price, fee, liquidation_leftover);

        FillEventCollector::LiquidationFillInput fill_in{};
        fill_in.close_qty = close_qty;
        fill_in.liq_price = liq_price;
        fill_in.fee = fee;
        fill_in.fee_rate = taker_fee;
        FillEventCollector::CaptureLiquidationFill(*this, liq_ord, fill_in);

        bool need_fallback_tiny_cleanup = false;
        auto it_pos_idx = position_index_by_id_.find(liq_ord.closing_position_id);
        if (it_pos_idx != position_index_by_id_.end()) {
            const size_t idx = it_pos_idx->second;
            if (idx < positions_.size() && positions_[idx].id == liq_ord.closing_position_id) {
                if (positions_[idx].quantity <= 1e-8) {
                    positions_.erase(positions_.begin() + static_cast<std::vector<Position>::difference_type>(idx));
                }
            }
            else {
                need_fallback_tiny_cleanup = true;
            }
        }
        else {
            need_fallback_tiny_cleanup = true;
        }
        if (need_fallback_tiny_cleanup) {
            positions_.erase(
                std::remove_if(positions_.begin(), positions_.end(),
                    [](const Position& p) { return p.quantity <= 1e-8; }),
                positions_.end());
        }

        merge_positions();
        rebuild_position_index_();

        const double updated_mark = get_last_mark_price_(liq_ord.symbol);
        if (updated_mark > 0.0) {
            update_unrealized_for_symbol_(liq_ord.symbol, updated_mark);
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
            const bool removed = filter_open_orders_([](const Order& o) {
                return o.instrument_type == InstrumentType::Perp;
                });
            if (removed) {
                open_orders_changed = true;
                mark_open_orders_dirty_();
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
