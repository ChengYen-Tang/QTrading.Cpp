#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

Account::Account(double initial_balance, int vip_level)
    : balance_(initial_balance), used_margin_(0.0), vip_level_(vip_level)
{
}

// Return the raw wallet balance. This can be negative in cross margin.
double Account::get_balance() const {
    return balance_;
}

// Sum all positions' unrealized PNL
double Account::total_unrealized_pnl() const {
    double total = 0.0;
    for (auto& pos : positions_) {
        total += pos.unrealized_pnl;
    }
    return total;
}

// Return total equity = wallet balance + total unrealized PNL
double Account::get_equity() const {
    return balance_ + total_unrealized_pnl();
}

// Retrieve the leverage for a specific symbol, or -1 if not set
double Account::get_symbol_leverage(const std::string& symbol) const {
    auto it = symbol_leverage_.find(symbol);
    if (it == symbol_leverage_.end()) {
        return -1.0;
    }
    return it->second;
}

// Set (or adjust) the leverage for a specific symbol
void Account::set_symbol_leverage(const std::string& symbol, double newLeverage) {
    if (newLeverage <= 0) {
        throw std::runtime_error("Leverage must be > 0.");
    }

    double oldLev = -1.0;
    auto it = symbol_leverage_.find(symbol);
    if (it != symbol_leverage_.end()) {
        oldLev = it->second;
    }

    // If not previously set, just assign
    if (oldLev < 0) {
        symbol_leverage_[symbol] = newLeverage;
        std::cout << "[set_symbol_leverage] " << symbol
            << " leverage set to " << newLeverage << "x.\n";
    }
    else {
        // If there's an existing position, we need to adjust margin usage
        bool success = adjust_position_leverage(symbol, oldLev, newLeverage);
        if (success) {
            symbol_leverage_[symbol] = newLeverage;
            std::cout << "[set_symbol_leverage] " << symbol
                << " changed leverage from " << oldLev << "x to "
                << newLeverage << "x.\n";
        }
        else {
            std::cerr << "[set_symbol_leverage] Failed to change leverage for "
                << symbol << " due to insufficient equity.\n";
        }
    }
}

// Place an order in cross margin, allowing negative wallet balance if offset by positive PNL
void Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    bool is_long,
    bool is_market_order)
{
    // Get or set default leverage for this symbol
    double lev = get_symbol_leverage(symbol);
    if (lev < 0) {
        lev = 20.0; // default
        symbol_leverage_[symbol] = lev;
        std::cout << "[place_order] " << symbol
            << " had no leverage set. Using default 20x.\n";
    }

    // Retrieve fee rates (maker/taker)
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();
    double feeRate = is_market_order ? takerFee : makerFee;

    // Notional value
    double notional = price * quantity;

    // Tier-based margin check
    double tier_init_rate, tier_maint_rate, tier_max_lev;
    std::tie(tier_init_rate, tier_maint_rate, tier_max_lev) = get_margin_rates(notional);

    // If user lever is above the tier's max leverage, reject
    if (lev > tier_max_lev) {
        std::cerr << "[place_order] " << symbol
            << " max allowed leverage is " << tier_max_lev
            << "x, but requested " << lev << "x. Rejected.\n";
        return;
    }

    // For cross margin, we simplify initial margin = notional / userLeverage
    double initial_margin = notional / lev;
    double maintenance_margin = notional * tier_maint_rate; // tier-based

    // Fee for opening this position
    double fee = notional * feeRate;

    // Check total equity (wallet balance + unrealized PnL)
    double equity = get_equity(); // can be > 0 even if balance_ < 0
    double total_required = initial_margin + fee;

    if (equity < total_required) {
        std::cerr << "[place_order] Insufficient equity to open position (need "
            << total_required << ", have " << equity << ").\n";
        return;
    }

    // Deduct from balance_ (may become negative if balance_ < required)
    balance_ -= total_required;

    // Increase used margin
    used_margin_ += initial_margin;

    Position pos{
        symbol,
        quantity,
        price,
        is_long,
        0.0,            // unrealized_pnl
        notional,
        initial_margin,
        maintenance_margin,
        fee,
        lev,
        feeRate
    };
    positions_.push_back(pos);

    std::cout << "[place_order] " << symbol
        << (is_long ? " LONG " : " SHORT ") << quantity
        << " @ " << price << (is_market_order ? " (Market)" : " (Limit)")
        << ", user leverage=" << lev << "x\n"
        << "  Required margin+fee=" << total_required
        << ", new balance=" << balance_ << " (may be negative)\n";
}

// Update positions with new price, recalc PnL, check for liquidation
void Account::update_positions(const MarketData::Kline& kline) {
    // 1) Recompute unrealized PnL for each open position
    for (auto& pos : positions_) {
        double current_price = kline.ClosePrice;
        double pnl = (current_price - pos.entry_price) * pos.quantity
            * (pos.is_long ? 1 : -1);
        pos.unrealized_pnl = pnl;
    }

    // 2) Check liquidation: if total equity < sum(maintenance_margin), 
    // we do a full liquidation. 
    double equity = get_equity();
    double total_maint = 0.0;
    for (auto& pos : positions_) {
        total_maint += pos.maintenance_margin;
    }

    if (equity < total_maint) {
        std::cerr << "[update_positions] Liquidation triggered! Equity="
            << equity << ", required maintenance=" << total_maint << "\n";
        // Full liquidation
        balance_ = 0.0;
        used_margin_ = 0.0;
        positions_.clear();
    }
}

// Close an existing position
void Account::close_position(const std::string& symbol, double price, bool is_market_order)
{
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();
    double feeRate = is_market_order ? takerFee : makerFee;

    for (auto it = positions_.begin(); it != positions_.end(); ++it) {
        if (it->symbol == symbol) {
            // Recalculate notional at close price (or you can keep original notional)
            double close_notional = price * it->quantity;
            double close_fee = close_notional * feeRate;

            // Realized PnL is the position's current unrealized PnL
            double realized_pnl = it->unrealized_pnl;

            // "Return" the initial margin, add realized PnL, subtract fee
            double amount_back = it->initial_margin + realized_pnl - close_fee;

            // Put that back to wallet balance (yes, it can become negative or positive)
            balance_ += amount_back;

            // Decrease used margin
            used_margin_ -= it->initial_margin;

            std::cout << "[close_position] " << symbol
                << (is_market_order ? " (Market)" : " (Limit)")
                << ", realized PnL=" << realized_pnl
                << ", close fee=" << close_fee
                << ", new balance=" << balance_ << "\n";

            positions_.erase(it);
            return;
        }
    }
    std::cerr << "[close_position] No position found for " << symbol << ".\n";
}

// Helper: get tier-based margin rates
std::tuple<double, double, double> Account::get_margin_rates(double notional) const {
    for (auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.initial_margin_rate,
                tier.maintenance_margin_rate,
                tier.max_leverage);
        }
    }
    // Fallback
    return std::make_tuple(0.125, 0.075, 8.0);
}

// Helper: get fee rates for the current VIP level (simplified to VIP 0)
std::tuple<double, double> Account::get_fee_rates() const {
    // If you wanted multiple VIP levels, you'd do:
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    // default to VIP 0
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
}

// Adjust an existing position's margin usage when changing leverage
bool Account::adjust_position_leverage(const std::string& symbol, double oldLev, double newLev) {
    bool found = false;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            found = true;
            double oldMargin = pos.initial_margin;
            double newMargin = pos.notional / newLev;
            double diff = newMargin - oldMargin;

            // If diff > 0, we need more margin
            if (diff > 0) {
                // Check if we have enough equity
                double equity = get_equity();
                // Just see if equity can cover the extra margin
                if ((equity - diff) < 0.0) {
                    return false;
                }
                // Deduct from balance_ (can go negative)
                balance_ -= diff;
                used_margin_ += diff;
            }
            else {
                // Releasing margin
                double release = std::fabs(diff);
                balance_ += release;
                used_margin_ -= release;
            }

            pos.initial_margin = newMargin;
            pos.leverage = newLev;
            // maintenance_margin doesn't change because notional is the same
        }
    }
    // If no position found, it's just setting leverage for a symbol with no position
    if (!found) {
        return true;
    }
    return true;
}
