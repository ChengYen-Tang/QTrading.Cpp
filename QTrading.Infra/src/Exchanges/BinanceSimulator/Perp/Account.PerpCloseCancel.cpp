#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <iostream>

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::PositionSide;

void Account::close_perp_position_(const std::string& symbol, double price)
{
    bool found = false;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it != position_indices_by_symbol_.end()) {
        for (size_t idx : it->second) {
            const auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            if (pos.instrument_type != InstrumentType::Perp) continue;
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    rebuild_open_order_index_();
    if (found) {
        ++state_version_;
        return;
    }
    if (enable_console_output_) {
        std::cerr << "[close_position] No perp position found for symbol=" << symbol << "\n";
    }
}

void Account::close_perp_position_side_(const std::string& symbol, PositionSide position_side, double price)
{
    const bool want_long = (position_side == PositionSide::Long);
    bool found = false;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it != position_indices_by_symbol_.end()) {
        for (size_t idx : it->second) {
            const auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            if (pos.instrument_type != InstrumentType::Perp) continue;
            if (pos.is_long != want_long) continue;
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    rebuild_open_order_index_();
    if (found) {
        ++state_version_;
        return;
    }
    if (enable_console_output_) {
        std::cerr << "[close_position] No " << (want_long ? "LONG" : "SHORT")
            << " perp position found for symbol=" << symbol << "\n";
    }
}

bool Account::cancel_perp_open_orders_(const std::string& symbol)
{
    const size_t before = open_orders_.size();
    open_orders_.erase(
        std::remove_if(open_orders_.begin(), open_orders_.end(),
            [&](const Order& o) {
                return o.symbol == symbol && o.instrument_type == InstrumentType::Perp;
            }),
        open_orders_.end());
    return open_orders_.size() != before;
}
