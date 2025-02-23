#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <unordered_map>

// --------------------------------------------
//  Constructor
// --------------------------------------------
Account::Account(double initial_balance, int vip_level)
    : balance_(initial_balance),
    used_margin_(0.0),
    vip_level_(vip_level),
    hedge_mode_(false),          // Default to one‑way mode
    next_order_id_(1),
    next_position_id_(1)
{
    open_orders_.reserve(1024);
    positions_.reserve(1024);
}

// --------------------------------------------
//  Basic account information functions
// --------------------------------------------
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

// --------------------------------------------
//  One‑way / Hedge mode
// --------------------------------------------
void Account::set_position_mode(bool hedgeMode) {
    // Disallow switching mode if there are open positions.
    if (!positions_.empty()) {
        std::cerr << "[set_position_mode] Cannot switch mode while positions are open.\n";
        return;
    }
    hedge_mode_ = hedgeMode;
    std::cout << "[set_position_mode] Now in "
        << (hedge_mode_ ? "HEDGE (dual) mode.\n" : "ONE‑WAY mode.\n");
}

bool Account::is_hedge_mode() const {
    return hedge_mode_;
}

// --------------------------------------------
//  Leverage functions
// --------------------------------------------
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

// --------------------------------------------
//  ID generators
// --------------------------------------------
int Account::generate_order_id() {
    return next_order_id_++;
}

int Account::generate_position_id() {
    return next_position_id_++;
}

// --------------------------------------------
//  place_order: Entry point
// --------------------------------------------
void Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    bool is_long,
    bool reduce_only)
{
    if (quantity <= 0) {
        std::cerr << "[place_order] Invalid quantity <= 0\n";
        return;
    }

    // In one‑way mode, attempt to process reverse orders.
    if (!hedge_mode_) {
        if (handleOneWayReverseOrder(symbol, quantity, price, is_long))
            return; // Reverse order processed.
    }

    // Otherwise, create a new order.
    int oid = generate_order_id();
    Order newOrd{
        oid,
        symbol,
        quantity,
        price,
        is_long,
        reduce_only,
        -1
    };
    open_orders_.push_back(newOrd);

    std::cout << "[place_order] Created Order ID=" << oid
        << (is_long ? " LONG " : " SHORT ")
        << ((price <= 0.0) ? "(Market)" : "(Limit)")
        << " qty=" << quantity << " " << symbol
        << (reduce_only ? " (reduceOnly)" : "")
        << " @ " << price << "\n";
}

void Account::place_order(const std::string& symbol, double quantity, bool is_long, bool reduce_only) {
    place_order(symbol, quantity, 0.0, is_long, reduce_only);
}

// --------------------------------------------
//  handleOneWayReverseOrder: Process reverse orders in one‑way mode.
// Returns true if the reverse order was handled.
// --------------------------------------------
bool Account::handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, bool is_long) {
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            if (pos.is_long == is_long) {
                // Same direction: treat as adding to position.
                return false;
            }
            // Reverse order: process reduction.
            double pos_qty = pos.quantity;
            double order_qty = quantity;
            if (order_qty < pos_qty) {
                int oid = generate_order_id();
                Order closingOrd{
                    oid,
                    symbol,
                    order_qty,
                    price,
                    pos.is_long,
                    false,
                    pos.id
                };
                open_orders_.push_back(closingOrd);
                std::cout << "[handleOneWayReverseOrder] Auto reduce position by " << order_qty
                    << " on pos_id=" << pos.id << "\n";
                return true;
            }
            else if (order_qty == pos_qty) {
                int oid = generate_order_id();
                Order closingOrd{
                    oid,
                    symbol,
                    order_qty,
                    price,
                    pos.is_long,
                    false,
                    pos.id
                };
                open_orders_.push_back(closingOrd);
                std::cout << "[handleOneWayReverseOrder] Auto close entire position (qty=" << order_qty
                    << ") pos_id=" << pos.id << "\n";
                return true;
            }
            else {
                // order_qty > pos_qty: first close the existing position, then open a new reverse position.
                double closeQty = pos_qty;
                {
                    int oid = generate_order_id();
                    Order closingOrd{
                        oid,
                        symbol,
                        closeQty,
                        price,
                        pos.is_long,
                        false,
                        pos.id
                    };
                    open_orders_.push_back(closingOrd);
                    std::cout << "[handleOneWayReverseOrder] Auto close entire position first.\n";
                }
                double newOpenQty = order_qty - closeQty;
                int openOid = generate_order_id();
                Order newOpen{
                    openOid,
                    symbol,
                    newOpenQty,
                    price,
                    is_long,   // New position direction
                    false,
                    -1
                };
                open_orders_.push_back(newOpen);
                std::cout << "[handleOneWayReverseOrder] Open new reversed position with qty=" << newOpenQty << "\n";
                return true;
            }
        }
    }
    return false;
}

// --------------------------------------------
//  place_closing_order: Generate a closing order for a given position.
// --------------------------------------------
void Account::place_closing_order(int position_id, double quantity, double price) {
    for (const auto& pos : positions_) {
        if (pos.id == position_id) {
            int oid = generate_order_id();
            Order closingOrd{
                oid,
                pos.symbol,
                quantity,
                price,
                pos.is_long,
                false,  // reduce_only is false for closing orders.
                position_id
            };
            open_orders_.push_back(closingOrd);
            std::cout << "[place_closing_order] Created closing order ID=" << oid
                << " to close pos_id=" << position_id
                << (pos.is_long ? " (LONG)" : " (SHORT)")
                << ((price <= 0.0) ? " (Market)" : " (Limit)")
                << " qty=" << quantity << " " << pos.symbol << " @ " << price << "\n";
            return;
        }
    }
    std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
}

// --------------------------------------------
//  merge_positions: Merge positions with the same symbol and direction.
// --------------------------------------------
void Account::merge_positions() {
    if (positions_.empty()) return;

    // Define a key by symbol and direction.
    struct Key {
        std::string sym;
        bool dir;
        bool operator==(const Key& o) const {
            return (sym == o.sym && dir == o.dir);
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            auto h1 = std::hash<std::string>()(k.sym);
            auto h2 = std::hash<bool>()(k.dir);
            return (h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2)));
        }
    };

    std::unordered_map<Key, Position, KeyHash> merged;
    for (auto& pos : positions_) {
        Key key{ pos.symbol, pos.is_long };
        if (merged.find(key) == merged.end()) {
            merged[key] = pos;
        }
        else {
            Position& p0 = merged[key];
            double totalQty = p0.quantity + pos.quantity;
            if (totalQty < 1e-8) {
                p0.quantity = 0;
                continue;
            }
            double weightedPrice = (p0.entry_price * p0.quantity + pos.entry_price * pos.quantity) / totalQty;
            p0.quantity = totalQty;
            p0.entry_price = weightedPrice;
            p0.notional += pos.notional;
            p0.initial_margin += pos.initial_margin;
            p0.maintenance_margin += pos.maintenance_margin;
            p0.fee += pos.fee;
        }
    }

    positions_.clear();
    positions_.reserve(merged.size());
    for (auto& kv : merged) {
        if (kv.second.quantity > 1e-8)
            positions_.push_back(kv.second);
    }
}

// --------------------------------------------
//  update_positions: Main matching and settlement logic.
// --------------------------------------------
void Account::update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume) {
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();

    std::vector<Order> leftover;
    leftover.reserve(open_orders_.size());

    // Process each open order.
    for (auto& ord : open_orders_) {
        auto itPrice = symbol_price_volume.find(ord.symbol);
        if (itPrice == symbol_price_volume.end()) {
            leftover.push_back(ord);
            continue;
        }
        double current_price = itPrice->second.first;
        double available_vol = itPrice->second.second;
        if (available_vol <= 0) {
            leftover.push_back(ord);
            continue;
        }

        bool is_market = (ord.price <= 0.0);
        bool can_fill = false;
        if (is_market) {
            can_fill = true;
        }
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

        double fill_qty = std::min(ord.quantity, available_vol);
        if (fill_qty < 1e-8) {
            leftover.push_back(ord);
            continue;
        }

        double fill_price = current_price;
        double notional = fill_qty * fill_price;
        double feeRate = (is_market ? takerFee : makerFee);
        double fee = notional * feeRate;

        if (ord.closing_position_id >= 0) {
            processClosingOrder(ord, fill_qty, fill_price, fee, leftover);
        }
        else {
            processOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
        }
    }

    open_orders_.swap(leftover);

    // Remove positions with negligible quantity.
    positions_.erase(
        std::remove_if(positions_.begin(), positions_.end(),
            [](const Position& p) { return p.quantity <= 1e-8; }),
        positions_.end()
    );

    merge_positions();

    // Recalculate unrealized PnL.
    for (auto& pos : positions_) {
        auto itp = symbol_price_volume.find(pos.symbol);
        if (itp != symbol_price_volume.end()) {
            double cp = itp->second.first;
            pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
    }

    double equity = get_equity();
    double totalMaint = 0.0;
    for (const auto& pos : positions_) {
        totalMaint += pos.maintenance_margin;
    }
    if (equity < totalMaint) {
        std::cerr << "[update_positions] Liquidation triggered! equity=" << equity
            << ", totalMaint=" << totalMaint << "\n";
        balance_ = 0.0;
        used_margin_ = 0.0;
        positions_.clear();
        open_orders_.clear();
        order_to_position_.clear();
    }

    std::cout << "[update_positions] equity=" << equity << ", totalMaint=" << totalMaint << "\n";
}

// --------------------------------------------
//  processClosingOrder: Process a closing order fill.
// --------------------------------------------
void Account::processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    bool foundPos = false;
    for (auto& pos : positions_) {
        if (pos.id == ord.closing_position_id) {
            foundPos = true;
            double close_qty = std::min(fill_qty, pos.quantity);
            double realized_pnl = (fill_price - pos.entry_price) * close_qty * (pos.is_long ? 1.0 : -1.0);
            double ratio = close_qty / pos.quantity;
            double freed_margin = pos.initial_margin * ratio;
            double freed_maint = pos.maintenance_margin * ratio;
            double freed_fee = pos.fee * ratio;
            double returned_amount = freed_margin + realized_pnl - fee;

            balance_ += returned_amount;
            used_margin_ -= freed_margin;

            pos.quantity -= close_qty;
            pos.initial_margin -= freed_margin;
            pos.maintenance_margin -= freed_maint;
            pos.fee -= freed_fee;
            pos.notional = pos.entry_price * pos.quantity;

            std::cout << "[processClosingOrder] Closing fill: pos_id=" << pos.id
                << ", closeQty=" << close_qty
                << ", realizedPnL=" << realized_pnl
                << ", closeFee=" << fee
                << ", new balance=" << balance_ << "\n";

            ord.quantity -= close_qty;
            if (ord.quantity > 1e-8)
                leftover.push_back(ord);
            break;
        }
    }
    if (!foundPos) {
        std::cerr << "[processClosingOrder] closing_position_id=" << ord.closing_position_id << " not found\n";
        leftover.push_back(ord);
    }
}

// --------------------------------------------
//  processReduceOnlyOrder: Process a reduce_only opening order fill.
// --------------------------------------------
bool Account::processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    for (auto& pos : positions_) {
        if (pos.symbol == ord.symbol && pos.is_long == ord.is_long) {
            double close_qty = std::min(fill_qty, pos.quantity);
            double realized_pnl = (fill_price - pos.entry_price) * close_qty * (pos.is_long ? 1.0 : -1.0);
            double ratio = close_qty / pos.quantity;
            double freed_margin = pos.initial_margin * ratio;
            double freed_maint = pos.maintenance_margin * ratio;
            double freed_fee = pos.fee * ratio;
            double returned_amount = freed_margin + realized_pnl - fee;

            balance_ += returned_amount;
            used_margin_ -= freed_margin;

            pos.quantity -= close_qty;
            pos.initial_margin -= freed_margin;
            pos.maintenance_margin -= freed_maint;
            pos.fee -= freed_fee;
            pos.notional = pos.entry_price * pos.quantity;

            std::cout << "[processReduceOnlyOrder] reduceOnly fill: reduced pos_id=" << pos.id
                << ", closeQty=" << close_qty
                << ", realizedPnL=" << realized_pnl
                << ", fee=" << fee << "\n";

            ord.quantity -= close_qty;
            if (ord.quantity > 1e-8)
                leftover.push_back(ord);
            return true;
        }
    }
    std::cout << "[processReduceOnlyOrder] reduce_only order has no matching position to reduce. Ignored.\n";
    return false;
}

// --------------------------------------------
//  processNormalOpeningOrder: Process a normal opening order fill.
// --------------------------------------------
void Account::processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover) {
    double lev = get_symbol_leverage(ord.symbol);
    double mmr, maxLev;
    std::tie(mmr, maxLev) = get_tier_info(notional);
    if (lev > maxLev) {
        std::cerr << "[processNormalOpeningOrder] Leverage too high for order id=" << ord.id << "\n";
        leftover.push_back(ord);
        return;
    }
    double init_margin = notional / lev;
    double maint_margin = notional * mmr;
    double required = init_margin + fee;
    double eq = get_equity();
    if (eq < required) {
        std::cerr << "[processNormalOpeningOrder] Not enough equity for order id=" << ord.id << "\n";
        leftover.push_back(ord);
        return;
    }
    balance_ -= required;
    used_margin_ += init_margin;

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
        for (auto& pos : positions_) {
            if (pos.id == pid) {
                double old_notional = pos.notional;
                double new_notional = old_notional + notional;
                double old_qty = pos.quantity;
                double new_qty = old_qty + fill_qty;
                double new_entry = new_notional / new_qty;

                pos.quantity = new_qty;
                pos.entry_price = new_entry;
                pos.notional += notional;
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

// --------------------------------------------
//  processOpeningOrder: Process an opening order fill.
// --------------------------------------------
void Account::processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover) {
    if (ord.reduce_only) {
        if (!processReduceOnlyOrder(ord, fill_qty, fill_price, fee, leftover)) {
            // If no matching position to reduce, ignore the order.
        }
    }
    else {
        processNormalOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
    }
}

// --------------------------------------------
//  close_position functions
// --------------------------------------------
void Account::close_position(const std::string& symbol, double price) {
    // In one‑way mode, close all positions for the symbol.
    // In hedge mode, this version can be customized to close both long and short positions.
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

void Account::close_position(const std::string& symbol, bool is_long, double price) {
    bool found = false;
    for (const auto& pos : positions_) {
        if (pos.symbol == symbol && pos.is_long == is_long) {
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    if (!found) {
        std::cerr << "[close_position] No " << (is_long ? "LONG" : "SHORT")
            << " position found for symbol=" << symbol << "\n";
    }
}

// --------------------------------------------
//  cancel_order_by_id: Cancel an open order by its ID.
// --------------------------------------------
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

// --------------------------------------------
//  Get all open orders & positions
// --------------------------------------------
const std::vector<Account::Order>& Account::get_all_open_orders() const {
    return open_orders_;
}

const std::vector<Account::Position>& Account::get_all_positions() const {
    return positions_;
}

// --------------------------------------------
//  Fee and tier helper functions
// --------------------------------------------
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

// --------------------------------------------
//  Adjust leverage for existing positions for a given symbol
// --------------------------------------------
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
            std::cerr << "[adjust_position_leverage] newLev=" << newLev << " > maxLev=" << maxLev << "\n";
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
