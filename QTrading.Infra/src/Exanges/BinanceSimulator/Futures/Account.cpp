#include "Exanges/BinanceSimulator/Futures/Config.hpp"
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

Account::Account(double initial_balance, int vip_level)
    : balance_(initial_balance), used_margin_(0.0), vip_level_(vip_level)
{
}

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

double Account::get_symbol_leverage(const std::string& symbol) const {
    auto it = symbol_leverage_.find(symbol);
    if (it == symbol_leverage_.end()) {
        return 1.0; // not set
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

    // If we already have a position for this symbol, adjust margin usage
    if (oldLev < 0) {
        // just set
        symbol_leverage_[symbol] = newLeverage;
        std::cout << "[set_symbol_leverage] " << symbol
            << " = " << newLeverage << "x\n";
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

void Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    bool is_long,
    bool is_market_order)
{
    // 1) Retrieve or assign user leverage
    double lev = get_symbol_leverage(symbol);

    // 2) Get maker/taker fees
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();
    double feeRate = (is_market_order ? takerFee : makerFee);

    double notional = price * quantity;

    // 3) Get tier-based maintenance_margin_rate, max_leverage
    double mmr, tier_max_lev;
    std::tie(mmr, tier_max_lev) = get_tier_info(notional);

    if (lev > tier_max_lev) {
        std::cerr << "[place_order] user leverage=" << lev
            << "x > tier max=" << tier_max_lev
            << "x. Rejected.\n";
        return;
    }

    // 4) initial_margin = notional / userLeverage
    double initial_margin = notional / lev;
    // maintenance margin = notional * mmr
    double maintenance_margin = notional * mmr;

    // 5) fees for opening
    double fee = notional * feeRate;

    // 6) Check if total equity >= (initial_margin + fee)
    double equity = get_equity();
    double required = initial_margin + fee;
    if (equity < required) {
        std::cerr << "[place_order] Not enough equity("
            << equity << ") < " << required << "\n";
        return;
    }

    // 7) Subtract from wallet balance (may go negative in cross)
    balance_ -= required;
    used_margin_ += initial_margin;

    // 8) Create the position
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
        << (is_long ? " LONG " : " SHORT ")
        << quantity << " @ " << price
        << (is_market_order ? " (Market)" : " (Limit)")
        << ", userLeverage=" << lev << "x\n"
        << "  margin+fee=" << required
        << ", new balance=" << balance_ << "\n";
}

void Account::update_positions(const std::map<std::string, double>& symbol_prices) {
    // 1) Recompute each position's unrealized PnL based on the correct symbol price
    for (auto& pos : positions_) {
        auto itPrice = symbol_prices.find(pos.symbol);
        if (itPrice == symbol_prices.end()) {
            // If no price for this symbol was provided, skip or handle error
            std::cerr << "[update_positions] Missing price for " << pos.symbol << "\n";
            continue;
        }
        double current_price = itPrice->second;
        double pnl = (current_price - pos.entry_price) * pos.quantity
            * (pos.is_long ? 1.0 : -1.0);
        pos.unrealized_pnl = pnl;
    }

    // 2) Check liquidation: if equity < sum of all maintenance margin => full liquidation
    double equity = get_equity();
    double total_maint = 0.0;
    for (auto& p : positions_) {
        total_maint += p.maintenance_margin;
    }

    if (equity < total_maint) {
        std::cerr << "[update_positions] Liquidation triggered."
            << " equity=" << equity
            << ", totalMaint=" << total_maint << "\n";
        // Full liquidation
        balance_ = 0.0;
        used_margin_ = 0.0;
        positions_.clear();
    }
}

void Account::close_position(const std::string& symbol, double price, bool is_market_order) {
    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();
    double feeRate = (is_market_order ? takerFee : makerFee);

    for (auto it = positions_.begin(); it != positions_.end(); ++it) {
        if (it->symbol == symbol) {
            // Compute "close notional" at current price
            double close_notional = price * it->quantity;
            // Closing fee
            double close_fee = close_notional * feeRate;

            // Realized PnL is the position's current unrealized
            double realized_pnl = it->unrealized_pnl;

            // Return margin + realized PnL, minus close fee
            double returned_amount = it->initial_margin + realized_pnl - close_fee;

            balance_ += returned_amount;
            used_margin_ -= it->initial_margin;

            std::cout << "[close_position] " << symbol
                << (is_market_order ? " (Market)" : " (Limit)")
                << ", realizedPnL=" << realized_pnl
                << ", closeFee=" << close_fee
                << ", new balance=" << balance_ << "\n";

            positions_.erase(it);
            return;
        }
    }
    std::cerr << "[close_position] No position found for symbol " << symbol << "\n";
}

/**
 * Retrieves "maintenance_margin_rate" and "max_leverage" from margin_tiers,
 * based on position's notional.
 */
std::tuple<double, double> Account::get_tier_info(double notional) const {
    // margin_tiers is sorted in ascending order by notional_upper
    for (auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    // fallback
    return std::make_tuple(margin_tiers.at(0).maintenance_margin_rate, margin_tiers.at(0).max_leverage);
}

/**
 * Retrieves maker/taker fees from vip_fee_rates (or default to VIP 0).
 */
std::tuple<double, double> Account::get_fee_rates() const {
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    // fallback: VIP 0
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
}

/**
 * When user changes leverage for a symbol that already has a position,
 * we adjust margin usage. If new leverage is lower, we free up some margin;
 * if it's higher, we require more margin.
 */
bool Account::adjust_position_leverage(const std::string& symbol, double oldLev, double newLev) {
    // Step 1: Gather all positions for this symbol
    std::vector<std::reference_wrapper<Position>> symbolPositions;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            symbolPositions.push_back(pos);
        }
    }
    if (symbolPositions.empty()) {
        // No positions for this symbol, so we can just set the new leverage
        return true;
    }

    // Variables to track total difference of margin
    double totalDiff = 0.0;   // if > 0, means we need additional margin
    // if < 0, means we release margin
// We'll also store each position's new maintenance margin 
// in case we need to revert.
    std::vector<double> newMaintMargins(symbolPositions.size());

    // Step 2: For each position, compute how margin usage would change
    for (size_t i = 0; i < symbolPositions.size(); i++) {
        Position& pos = symbolPositions[i].get();

        double oldMargin = pos.initial_margin;
        double newMargin = pos.notional / newLev; // Since notional is constant

        // Check the tier for notional => get maintenance_margin_rate, max_leverage
        double mmr, tier_max_lev;
        std::tie(mmr, tier_max_lev) = get_tier_info(pos.notional);

        // If newLeverage exceeds tier max, fail immediately (rollback).
        if (newLev > tier_max_lev) {
            std::cerr << "[adjust_position_leverage] newLev=" << newLev
                << "x > tier_max_lev=" << tier_max_lev << "x for notional="
                << pos.notional << " => cannot change.\n";
            return false; // no changes made
        }

        // We'll calculate how maintenance margin changes as well
        double newMaintMargin = pos.notional * mmr;
        newMaintMargins[i] = newMaintMargin;

        double diff = newMargin - oldMargin;
        totalDiff += diff; // sum up all differences
    }

    // Step 3: Check if we have enough equity if totalDiff > 0
    if (totalDiff > 0) {
        double eq = get_equity(); // could become negative balance, but we still need eq >= totalDiff
        if (eq < totalDiff) {
            std::cerr << "[adjust_position_leverage] Not enough equity. Need "
                << totalDiff << ", have " << eq << ".\n";
            return false; // fail => no partial changes
        }
    }

    // Step 4: If we pass all checks, apply changes to *all* positions
    // This ensures atomicity: either all are updated or none.
    if (totalDiff > 0) {
        // we need more margin
        balance_ -= totalDiff;    // can go negative if eq covers it
        used_margin_ += totalDiff;
    }
    else {
        // we release margin
        double release = std::fabs(totalDiff);
        balance_ += release;
        used_margin_ -= release;
    }

    // Step 5: Update each position's initial_margin, leverage, maintenance_margin
    for (size_t i = 0; i < symbolPositions.size(); i++) {
        Position& pos = symbolPositions[i].get();
        double newMargin = pos.notional / newLev;
        pos.initial_margin = newMargin;
        pos.leverage = newLev;
        pos.maintenance_margin = newMaintMargins[i];
    }

    return true;
}
