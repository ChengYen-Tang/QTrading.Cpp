#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <iostream>

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::PositionSide;

void Account::close_perp_position_(const std::string& symbol,
    double price,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode,
    bool close_position)
{
    bool found = false;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it != position_indices_by_symbol_.end()) {
        for (size_t idx : it->second) {
            const auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            if (pos.instrument_type != InstrumentType::Perp) continue;
            found = true;
            std::string derived_client_order_id;
            if (!client_order_id.empty()) {
                derived_client_order_id = client_order_id + ":" + std::to_string(pos.id);
            }
            place_closing_order(pos.id, pos.quantity, price, derived_client_order_id, stp_mode, close_position);
        }
    }
    if (found) {
        ++state_version_;
        return;
    }
    if (enable_console_output_) {
        std::cerr << "[close_position] No perp position found for symbol=" << symbol << "\n";
    }
}

void Account::close_perp_position_side_(const std::string& symbol,
    PositionSide position_side,
    double price,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode,
    bool close_position)
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
            std::string derived_client_order_id;
            if (!client_order_id.empty()) {
                derived_client_order_id = client_order_id + ":" + std::to_string(pos.id);
            }
            place_closing_order(pos.id, pos.quantity, price, derived_client_order_id, stp_mode, close_position);
        }
    }
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
    return filter_open_orders_([&](const Order& o) {
        return o.symbol == symbol && o.instrument_type == InstrumentType::Perp;
        });
}
