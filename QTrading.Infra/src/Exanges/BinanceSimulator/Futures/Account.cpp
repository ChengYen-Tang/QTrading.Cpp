#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <unordered_map>

// Constructor: Reserve capacity to reduce dynamic memory allocations.
Account::Account(double initial_balance, int vip_level)
    : balance_(initial_balance),
    used_margin_(0.0),
    vip_level_(vip_level),
    next_order_id_(1),
    next_position_id_(1)
{
    open_orders_.reserve(1024);
    positions_.reserve(1024);
}

// ------------------- Basic Getters -------------------
double Account::get_balance() const {
    return balance_;
}

double Account::total_unrealized_pnl() const {
    double total = 0.0;
    for (const auto& pos : positions_) {
        total += pos.unrealized_pnl;
    }
    return total;
}

double Account::get_equity() const {
    return balance_ + total_unrealized_pnl();
}

// ------------------- Leverage -------------------
double Account::get_symbol_leverage(const std::string& symbol) const {
    auto it = symbol_leverage_.find(symbol);
    return (it != symbol_leverage_.end()) ? it->second : 1.0;
}

void Account::set_symbol_leverage(const std::string& symbol, double newLeverage) {
    if (newLeverage <= 0)
        throw std::runtime_error("Leverage must be > 0.");
    double oldLev = 1.0;
    auto it = symbol_leverage_.find(symbol);
    if (it != symbol_leverage_.end()) {
        oldLev = it->second;
    }
    if (it == symbol_leverage_.end()) {
        symbol_leverage_[symbol] = newLeverage;
        std::cout << "[set_symbol_leverage] " << symbol << " = " << newLeverage << "x\n";
    }
    else {
        if (adjust_position_leverage(symbol, oldLev, newLeverage)) {
            it->second = newLeverage;
            std::cout << "[set_symbol_leverage] " << symbol << " changed from " << oldLev << "x -> " << newLeverage << "x\n";
        }
        else {
            std::cerr << "[set_symbol_leverage] Not enough equity to adjust.\n";
        }
    }
}

// ------------------- ID Generators -------------------
int Account::generate_order_id() {
    return next_order_id_++;
}

int Account::generate_position_id() {
    return next_position_id_++;
}

// ------------------- place_order -------------------
void Account::place_order(const std::string& symbol, double quantity, double price, bool is_long) {
    if (quantity <= 0) {
        std::cerr << "[place_order] Invalid quantity <= 0\n";
        return;
    }
    int oid = generate_order_id();
    // Market order if price <= 0.
    Order newOrd{ oid, symbol, quantity, price, is_long, -1 };
    open_orders_.push_back(newOrd);
    std::cout << "[place_order] Created Order ID=" << oid
        << (is_long ? " LONG " : " SHORT ")
        << ((price <= 0.0) ? "(Market)" : "(Limit)")
        << " " << quantity << " " << symbol << " @ " << price << "\n";
}

void Account::place_order(const std::string& symbol, double quantity, bool is_long) {
    place_order(symbol, quantity, 0.0, is_long);
}

// ------------------- place_closing_order -------------------
// Internal helper to create a closing order for a given position.
void Account::place_closing_order(int position_id, double quantity, double price) {
    for (const auto& pos : positions_) {
        if (pos.id == position_id) {
            int oid = generate_order_id();
            Order closingOrd{ oid, pos.symbol, quantity, price, pos.is_long, position_id };
            open_orders_.push_back(closingOrd);
            std::cout << "[place_closing_order] Created closing order ID=" << oid
                << " to close pos_id=" << position_id
                << (pos.is_long ?  " LONG " : " SHORT ")
                << ((price <= 0.0) ? "(Market)" : "(Limit)")
                << " " << quantity << " " << pos.symbol << " @ " << price << "\n";
            return;
        }
    }
    std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
}

// ------------------- update_positions -------------------
// Optimized matching engine using unordered_map for O(1) lookups and index-based iteration.
void Account::update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume) {
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();

    std::vector<Order> leftover;
    leftover.reserve(open_orders_.size());

    // Iterate over open_orders_ by index.
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        Order& ord = open_orders_[i];
        auto itPrice = symbol_price_volume.find(ord.symbol);
        if (itPrice == symbol_price_volume.end()) {
            leftover.push_back(ord);
            continue;
        }
        const double current_price = itPrice->second.first;
        double available_vol = itPrice->second.second;
        if (available_vol <= 0) {
            leftover.push_back(ord);
            continue;
        }
        bool is_market = (ord.price <= 0.0);
        bool can_fill = false;
        if (is_market)
            can_fill = true;
        else {
            if (ord.is_long && current_price <= ord.price)
                can_fill = true;
            else if (!ord.is_long && current_price >= ord.price)
                can_fill = true;
        }
        if (!can_fill) {
            leftover.push_back(ord);
            continue;
        }
        // Determine fill quantity (partial fill possible)
        double fill_qty = std::min(ord.quantity, available_vol);
        if (fill_qty <= 1e-8) {
            leftover.push_back(ord);
            continue;
        }
        double fill_price = current_price;
        double notional = fill_qty * fill_price;
        double feeRate = (is_market ? takerFee : makerFee);
        double fee = notional * feeRate;

        // Case A: Closing Order
        if (ord.closing_position_id >= 0) {
            bool foundPos = false;
            for (size_t j = 0; j < positions_.size(); ++j) {
                Position& pos = positions_[j];
                if (pos.id == ord.closing_position_id) {
                    foundPos = true;
                    double close_qty = std::min(fill_qty, pos.quantity);
                    double realized_pnl = (fill_price - pos.entry_price) * close_qty * (pos.is_long ? 1.0 : -1.0);
                    double ratio = close_qty / pos.quantity;
                    double freed_margin = pos.initial_margin * ratio;
                    double freed_maint = pos.maintenance_margin * ratio;
                    double freed_fee = pos.fee * ratio; // approximate
                    double returned_amount = freed_margin + realized_pnl - fee;

                    balance_ += returned_amount;
                    used_margin_ -= freed_margin;

                    pos.quantity -= close_qty;
                    pos.initial_margin -= freed_margin;
                    pos.maintenance_margin -= freed_maint;
                    pos.fee -= freed_fee;
                    pos.notional = pos.entry_price * pos.quantity;

                    std::cout << "[update_positions] Closing fill: pos_id=" << pos.id
                        << ", closeQty=" << close_qty
                        << ", realizedPnL=" << realized_pnl
                        << ", closeFee=" << fee
                        << ", new balance=" << balance_ << "\n";

                    ord.quantity -= close_qty;
                    if (ord.quantity > 1e-8) {
                        leftover.push_back(ord);
                    }
                    break;
                }
            }
            if (!foundPos) {
                std::cerr << "[update_positions] closing_position_id=" << ord.closing_position_id << " not found\n";
                leftover.push_back(ord);
            }
        }
        // Case B: Opening Order
        else {
            double lev = get_symbol_leverage(ord.symbol);
            double mmr, maxLev;
            std::tie(mmr, maxLev) = get_tier_info(notional);
            if (lev > maxLev) {
                std::cerr << "[update_positions] Leverage too high for order id=" << ord.id << "\n";
                leftover.push_back(ord);
                continue;
            }
            double init_margin = notional / lev;
            double maint_margin = notional * mmr;
            double required = init_margin + fee;
            double eq = get_equity();
            if (eq < required) {
                std::cerr << "[update_positions] Not enough equity for order id=" << ord.id << "\n";
                leftover.push_back(ord);
                continue;
            }
            balance_ -= required;
            used_margin_ += init_margin;

            // If this order has a previous partial fill, merge into the same position.
            auto pm = order_to_position_.find(ord.id);
            if (pm == order_to_position_.end()) {
                int pid = generate_position_id();
                positions_.push_back(Position{
                    pid,
                    ord.id,
                    ord.symbol,
                    fill_qty,
                    fill_price,
                    ord.is_long,
                    0.0, // unrealized PnL will be recalculated
                    notional,
                    init_margin,
                    maint_margin,
                    fee,
                    lev,
                    feeRate
                    });
                order_to_position_[ord.id] = pid;
            }
            else {
                int pid = pm->second;
                for (size_t j = 0; j < positions_.size(); ++j) {
                    Position& pos = positions_[j];
                    if (pos.id == pid) {
                        double old_notional = pos.notional;
                        double new_notional = old_notional + notional;
                        double old_qty = pos.quantity;
                        double new_qty = old_qty + fill_qty;
                        double new_entry = new_notional / new_qty;

                        pos.quantity = new_qty;
                        pos.entry_price = new_entry;
                        pos.notional = new_notional;
                        pos.initial_margin += init_margin;
                        pos.maintenance_margin += maint_margin;
                        pos.fee += fee;
                        break;
                    }
                }
            }
            double leftoverQty = ord.quantity - fill_qty;
            if (leftoverQty > 1e-8) {
                ord.quantity = leftoverQty;
                leftover.push_back(ord);
            }
        }
    }
    open_orders_.swap(leftover);

    // Remove positions with near-zero quantity.
    positions_.erase(
        std::remove_if(positions_.begin(), positions_.end(),
            [](const Position& p) { return p.quantity <= 1e-8; }),
        positions_.end()
    );

    // Recalculate unrealized PnL for all positions.
    for (auto& pos : positions_) {
        auto itp = symbol_price_volume.find(pos.symbol);
        if (itp != symbol_price_volume.end()) {
            double cp = itp->second.first;
            pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
    }

    // Liquidation check.
    double equity = get_equity();
    double totalMaint = 0.0;
    for (const auto& pos : positions_) {
        totalMaint += pos.maintenance_margin;
    }
    if (equity < totalMaint) {
        std::cerr << "[update_positions] Liquidation triggered! equity=" << equity << ", totalMaint=" << totalMaint << "\n";
        balance_ = 0.0;
        used_margin_ = 0.0;
        positions_.clear();
        open_orders_.clear();
        order_to_position_.clear();
    }
    std::cout << "[update_positions] equity=" << equity << ", totalMaint=" << totalMaint << "\n";
}

// ------------------- close_position (by symbol) -------------------
void Account::close_position(const std::string& symbol, double price) {
    bool found = false;
    for (const auto& pos : positions_) {
        if (pos.symbol == symbol) {
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    if (!found) {
        std::cerr << "[close_position] No position found for symbol=" << symbol << "\n";
    }
}

void Account::close_position(const std::string& symbol) {
    close_position(symbol, 0.0);
}

// ------------------- close_position_by_id -------------------
void Account::close_position_by_id(int position_id, double price) {
    for (const auto& pos : positions_) {
        if (pos.id == position_id) {
            place_closing_order(position_id, pos.quantity, price);
            return;
        }
    }
    std::cerr << "[close_position_by_id] No position found with ID=" << position_id << "\n";
}

void Account::close_position_by_id(int position_id) {
    close_position_by_id(position_id, 0.0);
}

// ------------------- get_all_open_orders & get_all_positions -------------------
const std::vector<Account::Order>& Account::get_all_open_orders() const {
    return open_orders_;
}

const std::vector<Account::Position>& Account::get_all_positions() const {
    return positions_;
}

// ------------------- cancel_order_by_id -------------------
void Account::cancel_order_by_id(int order_id) {
    bool found = false;
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        if (it->id == order_id) {
            found = true;
            std::cout << "[cancel_order_by_id] Canceling order ID=" << order_id
                << ", leftover qty=" << it->quantity << "\n";
            it = open_orders_.erase(it);
        }
        else {
            ++it;
        }
    }
    if (!found) {
        std::cerr << "[cancel_order_by_id] No open order with ID=" << order_id << "\n";
    }
}

// ------------------- Fee & Tier Helpers -------------------
std::tuple<double, double> Account::get_tier_info(double notional) const {
    for (const auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    return std::make_tuple(margin_tiers.front().maintenance_margin_rate, margin_tiers.front().max_leverage);
}

std::tuple<double, double> Account::get_fee_rates() const {
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
}

// ------------------- adjust_position_leverage -------------------
bool Account::adjust_position_leverage(const std::string& symbol, double oldLev, double newLev) {
    std::vector<std::reference_wrapper<Position>> related;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            related.push_back(pos);
        }
    }
    if (related.empty())
        return true;

    double totalDiff = 0.0;
    std::vector<double> newMaint(related.size());
    for (size_t i = 0; i < related.size(); ++i) {
        Position& p = related[i].get();
        double mmr, maxLev;
        std::tie(mmr, maxLev) = get_tier_info(p.notional);
        if (newLev > maxLev) {
            std::cerr << "[adjust_position_leverage] newLev=" << newLev
                << " > maxLev=" << maxLev << "\n";
            return false;
        }
        double oldM = p.initial_margin;
        double newM = p.notional / newLev;
        double diff = newM - oldM;
        totalDiff += diff;
        newMaint[i] = p.notional * mmr;
    }
    double eq = get_equity();
    if (totalDiff > 0) {
        if (eq < totalDiff) {
            std::cerr << "[adjust_position_leverage] Not enough equity.\n";
            return false;
        }
        balance_ -= totalDiff;
        used_margin_ += totalDiff;
    }
    else {
        balance_ += std::fabs(totalDiff);
        used_margin_ -= std::fabs(totalDiff);
    }
    for (size_t i = 0; i < related.size(); ++i) {
        Position& p = related[i].get();
        p.initial_margin = p.notional / newLev;
        p.leverage = newLev;
        p.maintenance_margin = newMaint[i];
    }
    return true;
}
