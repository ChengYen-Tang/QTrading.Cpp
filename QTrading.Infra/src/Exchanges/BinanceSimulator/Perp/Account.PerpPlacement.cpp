#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <iostream>

using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

bool Account::place_perp_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode,
    const QTrading::Dto::Trading::InstrumentSpec& instrument_spec)
{
    if (!instrument_spec.allow_short && side == OrderSide::Sell) {
        double held_qty = 0.0;
        int held_position_id = -1;
        auto it = position_indices_by_symbol_.find(symbol);
        if (it != position_indices_by_symbol_.end()) {
            for (size_t idx : it->second) {
                if (idx >= positions_.size()) {
                    continue;
                }
                const auto& p = positions_[idx];
                if (p.symbol != symbol || !p.is_long || p.quantity <= 1e-8) {
                    continue;
                }
                held_qty += p.quantity;
                if (held_position_id < 0) {
                    held_position_id = p.id;
                }
            }
        }

        const double pending_close_qty = pending_close_sell_qty_for_symbol_(symbol);

        const double sellable_qty = std::max(0.0, held_qty - pending_close_qty);
        if (sellable_qty <= 1e-8) {
            if (enable_console_output_) {
                std::cerr << "[place_order] Spot sell rejected: no available spot inventory.\n";
            }
            return reject_order_(OrderRejectInfo::Code::SpotNoInventory, "Spot sell rejected: no available spot inventory");
        }
        if (quantity > sellable_qty + 1e-8) {
            if (enable_console_output_) {
                std::cerr << "[place_order] Spot sell rejected: quantity exceeds available inventory.\n";
            }
            return reject_order_(OrderRejectInfo::Code::SpotQuantityExceedsInventory, "Spot sell rejected: quantity exceeds available inventory");
        }
        if (held_position_id < 0) {
            if (enable_console_output_) {
                std::cerr << "[place_order] Spot sell rejected: no long position to close.\n";
            }
            return reject_order_(OrderRejectInfo::Code::SpotNoLongPositionToClose, "Spot sell rejected: no long position to close");
        }

        const int oid = generate_order_id();
        Order closing_ord{
            oid,
            symbol,
            quantity,
            price,
            OrderSide::Sell,
            PositionSide::Both,
            reduce_only,
            held_position_id
        };
        closing_ord.instrument_type = instrument_spec.type;
        closing_ord.client_order_id = client_order_id;
        closing_ord.stp_mode = static_cast<int>(stp_mode);
        append_open_order_(std::move(closing_ord));
        mark_open_orders_dirty_();
        ++state_version_;
        return true;
    }

    if (!hedge_mode_ && position_side != PositionSide::Both) {
        position_side = PositionSide::Both;
    }

    // Binance-like: in hedge mode the caller must specify Long/Short (no inference).
    if (hedge_mode_ && position_side == PositionSide::Both) {
        if (enable_console_output_) {
            std::cerr << "[place_order] Hedge-mode order must specify position_side (Long/Short).\n";
        }
        return reject_order_(OrderRejectInfo::Code::HedgeModePositionSideRequired, "Hedge-mode order must specify position_side (Long/Short)");
    }
    if (strict_binance_mode_ && hedge_mode_ && reduce_only) {
        if (enable_console_output_) {
            std::cerr << "[place_order] Hedge-mode reduce_only is disabled in strict Binance mode.\n";
        }
        return reject_order_(OrderRejectInfo::Code::StrictHedgeReduceOnlyDisabled, "Hedge-mode reduce_only is disabled in strict Binance mode");
    }

    // In one-way mode, attempt to process reverse (flip) orders.
    if (!hedge_mode_) {
        if (handleOneWayReverseOrder(symbol, quantity, price, side, client_order_id, stp_mode)) {
            return true;
        }
    }

    // reduceOnly reject policy on placement (do not add to open_orders_).
    if (reduce_only) {
        Order check{
            -1,
            symbol,
            quantity,
            price,
            side,
            position_side,
            true,
            -1
        };

        if (!has_reducible_position_for_order_(check)) {
            if (enable_console_output_) {
                std::cerr << "[place_order] reduce_only rejected: no reducible position.\n";
            }
            return reject_order_(OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition, "reduce_only rejected: no reducible position");
        }
    }

    const int oid = generate_order_id();
    Order new_ord{
        oid,
        symbol,
        quantity,
        price,
        side,
        position_side,
        reduce_only,
        -1
    };
    new_ord.instrument_type = instrument_spec.type;
    new_ord.client_order_id = client_order_id;
    new_ord.stp_mode = static_cast<int>(stp_mode);
    append_open_order_(std::move(new_ord));
    mark_open_orders_dirty_();
    ++state_version_;
    return true;
}
