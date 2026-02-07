#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <iostream>

using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

/// @brief Handle a reverse order in one-way mode (auto-reduce or reverse).
/// @param symbol    Trading symbol.
/// @param quantity  Order quantity.
/// @param price     Order price.
/// @param is_long   Direction of the new order.
/// @return true if the order was converted into a closing/reverse order.
bool Account::handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, OrderSide side) {
    // In one-way mode we maintain at most one net position per symbol.
    const auto symbol_type = resolve_instrument_spec_(symbol).type;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it == position_indices_by_symbol_.end() || it->second.empty()) {
        return false;
    }

    Position& pos = positions_[it->second.front()];
    const bool posIsLong = pos.is_long;
    const bool orderIsBuy = (side == OrderSide::Buy);

    // Same-direction add: BUY with long position, or SELL with short position.
    if ((posIsLong && orderIsBuy) || (!posIsLong && !orderIsBuy)) {
        return false;
    }

    // Reverse-direction: this is a reduce/flip.
    const double pos_qty = pos.quantity;
    const double order_qty = quantity;

    // This is a closing action, so create a closing order targeting the existing position id.
    // Closing direction is opposite the position: long closes via SELL, short closes via BUY.
    const OrderSide closeSide = posIsLong ? OrderSide::Sell : OrderSide::Buy;

    if (order_qty <= pos_qty) {
        const int oid = generate_order_id();
        Order closingOrd{
            oid,
            symbol,
            order_qty,
            price,
            closeSide,
            PositionSide::Both,
            false,
            pos.id
        };
        closingOrd.instrument_type = symbol_type;
        open_orders_.push_back(closingOrd);
        mark_open_orders_dirty_();
        return true;
    }

    // order_qty > pos_qty: first close the existing position, then open a new reverse position.
    {
        const int oid = generate_order_id();
        Order closingOrd{
            oid,
            symbol,
            pos_qty,
            price,
            closeSide,
            PositionSide::Both,
            false,
            pos.id
        };
        closingOrd.instrument_type = symbol_type;
        open_orders_.push_back(closingOrd);
    }

    const double newOpenQty = order_qty - pos_qty;
    const OrderSide openSide = side;
    {
        const int openOid = generate_order_id();
        Order newOpen{
            openOid,
            symbol,
            newOpenQty,
            price,
            openSide,
            PositionSide::Both,
            false,
            -1
        };
        newOpen.instrument_type = symbol_type;
        open_orders_.push_back(newOpen);
    }
    mark_open_orders_dirty_();
    return true;
}

/// @brief Create a closing order for a specific position.
/// @param position_id  ID of the position to close.
/// @param quantity     Amount to close.
/// @param price        Close price (<=0 = market).
void Account::place_closing_order(int position_id, double quantity, double price) {
    auto it = position_index_by_id_.find(position_id);
    if (it != position_index_by_id_.end()) {
        const Position& pos = positions_[it->second];
        const int oid = generate_order_id();
        const OrderSide closeSide = pos.is_long ? OrderSide::Sell : OrderSide::Buy;
        Order closingOrd{
            oid,
            pos.symbol,
            quantity,
            price,
            closeSide,
            hedge_mode_ ? (pos.is_long ? PositionSide::Long : PositionSide::Short) : PositionSide::Both,
            false,
            position_id
        };
        closingOrd.instrument_type = pos.instrument_type;
        open_orders_.push_back(closingOrd);
        mark_open_orders_dirty_();
        return;
    }
    if (enable_console_output_) {
        std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
    }
}
