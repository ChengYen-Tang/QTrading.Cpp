#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

Account::Account(double initial_balance, int vip_level)
    : balance_(initial_balance),
    used_margin_(0.0),
    vip_level_(vip_level),
    next_order_id_(1),
    next_position_id_(1)
{
}

/* ------------------- Basic Getters ------------------- */
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

/* ------------------- Leverage ------------------- */
double Account::get_symbol_leverage(const std::string& symbol) const {
    auto it = symbol_leverage_.find(symbol);
    if (it == symbol_leverage_.end()) {
        return 1.0; // default
    }
    return it->second;
}
void Account::set_symbol_leverage(const std::string& symbol, double newLeverage) {
    if (newLeverage <= 0) {
        throw std::runtime_error("Leverage must be > 0.");
    }
    double oldLev = 1.0;
    auto it = symbol_leverage_.find(symbol);
    if (it != symbol_leverage_.end()) {
        oldLev = it->second;
    }

    if (symbol_leverage_.find(symbol) == symbol_leverage_.end()) {
        symbol_leverage_[symbol] = newLeverage;
        std::cout << "[set_symbol_leverage] " << symbol << " = " << newLeverage << "x\n";
    }
    else {
        bool success = adjust_position_leverage(symbol, oldLev, newLeverage);
        if (success) {
            symbol_leverage_[symbol] = newLeverage;
            std::cout << "[set_symbol_leverage] " << symbol
                << " changed from " << oldLev << "x -> "
                << newLeverage << "x\n";
        }
        else {
            std::cerr << "[set_symbol_leverage] Not enough equity to adjust.\n";
        }
    }
}

/* ------------------- ID Generators ------------------- */
int Account::generate_order_id() {
    return next_order_id_++;
}
int Account::generate_position_id() {
    return next_position_id_++;
}

/* ----------------------------------------------------
   place_order():
   - price <= 0 => market
   - price > 0  => limit
   - closing_position_id = -1 => normal opening order
-----------------------------------------------------*/
void Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    bool is_long)
{
    if (quantity <= 0) {
        std::cerr << "[place_order] Invalid quantity <= 0\n";
        return;
    }
    int oid = generate_order_id();
    std::string ordType = (price <= 0.0) ? "(Market)" : "(Limit)";
    Order newOrd{
        oid,
        symbol,
        quantity,
        price,
        is_long,
        -1 // normal order
    };

    open_orders_.push_back(newOrd);

    std::cout << "[place_order] Created Order ID=" << oid
        << (is_long ? " LONG " : " SHORT ")
        << ordType << " " << quantity << " " << symbol
        << " @ " << price << "\n";
}

/* ----------------------------------------------------
   place_closing_order() - internal helper
   - Opposite direction from the position
   - No margin required, realize PnL on fill
-----------------------------------------------------*/
void Account::place_closing_order(int position_id, double quantity, double price)
{
    // find the position
    for (auto& pos : positions_) {
        if (pos.id == position_id) {
            int oid = generate_order_id();
            // opposite direction
            bool oppositeDir = !pos.is_long;
            std::string ordType = (price <= 0.0) ? "(Market)" : "(Limit)";

            Order closingOrd{
                oid,
                pos.symbol,
                quantity,
                price,
                oppositeDir,
                position_id // link to a position => closing
            };
            open_orders_.push_back(closingOrd);

            std::cout << "[place_closing_order] Created closing order ID=" << oid
                << " => close pos_id=" << position_id
                << (oppositeDir ? " SHORT " : " LONG ")
                << ordType << " " << quantity << " " << pos.symbol
                << " @ " << price << "\n";
            return;
        }
    }
    std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
}

/* ----------------------------------------------------
   update_positions():
   - For each order:
       if closing_position_id != -1 => offset that position
       else => normal open
   - partial fill logic
   - recompute PnL
   - check liquidation
-----------------------------------------------------*/
void Account::update_positions(const std::map<std::string, std::pair<double, double>>& symbol_price_volume)
{
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();

    std::vector<Order> leftover;
    leftover.reserve(open_orders_.size());

    for (auto& ord : open_orders_) {
        // check market data
        auto it = symbol_price_volume.find(ord.symbol);
        if (it == symbol_price_volume.end()) {
            leftover.push_back(ord);
            continue;
        }
        double current_price = it->second.first;
        double available_vol = it->second.second;
        if (available_vol <= 0) {
            leftover.push_back(ord);
            continue;
        }

        // Decide market or limit
        bool is_market = (ord.price <= 0.0);
        bool can_fill = false;
        if (is_market) {
            can_fill = true;
        }
        else {
            // limit logic
            if (ord.is_long && current_price <= ord.price) {
                can_fill = true;
            }
            else if (!ord.is_long && current_price >= ord.price) {
                can_fill = true;
            }
        }
        if (!can_fill) {
            leftover.push_back(ord);
            continue;
        }

        // partial fill
        double fill_qty = std::min(ord.quantity, available_vol);
        if (fill_qty <= 0) {
            leftover.push_back(ord);
            continue;
        }

        double fill_price = current_price; // simplified
        double notional = fill_qty * fill_price;

        // fee
        double feeRate = (is_market ? takerFee : makerFee);
        double fee = notional * feeRate;

        // ----------- Case A: Closing Order ------------
        if (ord.closing_position_id >= 0) {
            bool foundPos = false;
            for (auto& pos : positions_) {
                if (pos.id == ord.closing_position_id) {
                    foundPos = true;
                    double close_qty = std::min(fill_qty, pos.quantity);

                    // realized PnL
                    double realized_pnl = (fill_price - pos.entry_price) * close_qty
                        * (pos.is_long ? 1.0 : -1.0);

                    double ratio = (close_qty / pos.quantity);
                    double freed_margin = pos.initial_margin * ratio;
                    double freed_maint = pos.maintenance_margin * ratio;
                    double freed_fee = pos.fee * ratio; // partial approach
                    double returned_amount = freed_margin + realized_pnl - fee;

                    balance_ += returned_amount;
                    used_margin_ -= freed_margin;

                    pos.quantity -= close_qty;
                    pos.initial_margin -= freed_margin;
                    pos.maintenance_margin -= freed_maint;
                    pos.fee -= freed_fee;
                    pos.notional = pos.entry_price * pos.quantity; // or recalc

                    std::cout << "[update_positions] Closing fill => pos_id=" << pos.id
                        << ", closeQty=" << close_qty
                        << ", realizedPnL=" << realized_pnl
                        << ", closeFee=" << fee
                        << ", new balance=" << balance_ << "\n";

                    // reduce order qty
                    ord.quantity -= close_qty;

                    // check if pos fully closed
                    if (pos.quantity <= 1e-8) {
                        // remove from order_to_position_
                        for (auto itMap = order_to_position_.begin(); itMap != order_to_position_.end(); ) {
                            if (itMap->second == pos.id) {
                                itMap = order_to_position_.erase(itMap);
                            }
                            else {
                                ++itMap;
                            }
                        }
                        // set quantity=0 => we erase it after loop
                        pos.quantity = 0.0;
                    }

                    if (ord.quantity > 1e-8) {
                        leftover.push_back(ord); // partial leftover
                    }
                    break;
                }
            }
            if (!foundPos) {
                std::cerr << "[update_positions] closing_position_id=" << ord.closing_position_id
                    << " not found\n";
                leftover.push_back(ord);
            }
        }
        // ----------- Case B: Opening Order -------------
        else {
            double lev = get_symbol_leverage(ord.symbol);
            double mmr, maxLev;
            std::tie(mmr, maxLev) = get_tier_info(notional);
            if (lev > maxLev) {
                std::cerr << "[update_positions] Leverage too high => skip.\n";
                leftover.push_back(ord);
                continue;
            }
            double init_margin = notional / lev;
            double maint_margin = notional * mmr;
            double required = init_margin + fee;

            double eq = get_equity();
            if (eq < required) {
                std::cerr << "[update_positions] Not enough equity => skip.\n";
                leftover.push_back(ord);
                continue;
            }

            // deduct margin + fee
            balance_ -= required;
            used_margin_ += init_margin;

            // find if we have an existing position for this order
            auto pm = order_to_position_.find(ord.id);
            if (pm == order_to_position_.end()) {
                // new position
                int pid = generate_position_id();
                Position newPos{
                    pid,
                    ord.id,
                    ord.symbol,
                    fill_qty,
                    fill_price,
                    ord.is_long,
                    0.0,  // unrealized
                    notional,
                    init_margin,
                    maint_margin,
                    fee,
                    lev,
                    feeRate
                };
                positions_.push_back(newPos);
                order_to_position_[ord.id] = pid;
            }
            else {
                // partial fill for existing position
                int pid = pm->second;
                for (auto& p : positions_) {
                    if (p.id == pid) {
                        double old_notional = p.notional;
                        double new_notional = old_notional + notional;
                        double old_qty = p.quantity;
                        double new_qty = old_qty + fill_qty;
                        double new_entry = new_notional / new_qty;

                        p.quantity = new_qty;
                        p.entry_price = new_entry;
                        p.notional = new_notional;
                        p.initial_margin += init_margin;
                        p.maintenance_margin += maint_margin;
                        p.fee += fee;
                        break;
                    }
                }
            }

            // leftover
            double leftoverQty = ord.quantity - fill_qty;
            if (leftoverQty > 1e-8) {
                ord.quantity = leftoverQty;
                leftover.push_back(ord);
            }
        }
    }

    // remove fully closed positions
    positions_.erase(
        std::remove_if(positions_.begin(), positions_.end(),
            [](const Position& pp) { return pp.quantity <= 1e-8; }),
        positions_.end()
    );

    // open_orders_ => leftover
    open_orders_ = std::move(leftover);

    // recompute unrealized PnL for all active positions
    for (auto& pos : positions_) {
        auto itp = symbol_price_volume.find(pos.symbol);
        if (itp == symbol_price_volume.end()) {
            continue;
        }
        double cur_price = itp->second.first;
        double pnl = (cur_price - pos.entry_price) * pos.quantity
            * (pos.is_long ? 1.0 : -1.0);
        pos.unrealized_pnl = pnl;
    }

    // liquidation check
    double equity = get_equity();
    double totalMaint = 0.0;
    for (auto& p : positions_) {
        totalMaint += p.maintenance_margin;
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
}

/* ----------------------------------------------------
   close_position(by symbol):
   - price <= 0 => market close
   - price > 0  => limit close
   - For each position on that symbol, create a closing order
-----------------------------------------------------*/
void Account::close_position(const std::string& symbol, double price) {
    bool found = false;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            found = true;
            double qtyToClose = pos.quantity;
            place_closing_order(pos.id, qtyToClose, price);
        }
    }
    if (!found) {
        std::cerr << "[close_position] No position found for symbol=" << symbol << "\n";
    }
}

/* ----------------------------------------------------
   close_position_by_id:
   - price <= 0 => market, else => limit
   - create a single closing order for that pos_id
-----------------------------------------------------*/
void Account::close_position_by_id(int position_id, double price) {
    for (auto& pos : positions_) {
        if (pos.id == position_id) {
            double qtyToClose = pos.quantity;
            place_closing_order(position_id, qtyToClose, price);
            return;
        }
    }
    std::cerr << "[close_position_by_id] No position found with ID=" << position_id << "\n";
}

/* ------------------- Queries & Cancel -------------------*/
const std::vector<Account::Order>& Account::get_all_open_orders() const {
    return open_orders_;
}
const std::vector<Account::Position>& Account::get_all_positions() const {
    return positions_;
}

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

/* ------------------- Fee & Tier Helpers ------------------- */
std::tuple<double, double> Account::get_tier_info(double notional) const {
    for (auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    // fallback
    return std::make_tuple(margin_tiers.at(0).maintenance_margin_rate,
        margin_tiers.at(0).max_leverage);
}
std::tuple<double, double> Account::get_fee_rates() const {
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    // fallback VIP0
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate,
        vip_fee_rates.at(0).taker_fee_rate);
}

/* ----------------------------------------------------
   adjust_position_leverage:
   - If user changes symbol's leverage => recalc margin
   - Not netting positions, just recalc each individually.
-----------------------------------------------------*/
bool Account::adjust_position_leverage(const std::string& symbol, double oldLev, double newLev) {
    std::vector<std::reference_wrapper<Position>> related;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            related.push_back(pos);
        }
    }
    if (related.empty()) {
        return true; // no positions => ok
    }

    double totalDiff = 0.0;
    std::vector<double> newMaint(related.size());

    for (size_t i = 0; i < related.size(); i++) {
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
        double diff = (newM - oldM);
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

    // apply
    for (size_t i = 0; i < related.size(); i++) {
        Position& p = related[i].get();
        p.initial_margin = p.notional / newLev;
        p.leverage = newLev;
        p.maintenance_margin = newMaint[i];
    }

    return true;
}
