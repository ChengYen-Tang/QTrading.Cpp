#include "Exchanges/BinanceSimulator/Futures/Account.hpp"

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

void Account::rebuild_open_order_index_()
{
    open_order_index_by_id_.clear();
    open_order_index_by_id_.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        open_order_index_by_id_[open_orders_[i].id] = i;
    }
}

void Account::rebuild_position_index_()
{
    position_index_by_id_.clear();
    position_index_by_id_.reserve(positions_.size());
    for (size_t i = 0; i < positions_.size(); ++i) {
        position_index_by_id_[positions_[i].id] = i;
    }
}
