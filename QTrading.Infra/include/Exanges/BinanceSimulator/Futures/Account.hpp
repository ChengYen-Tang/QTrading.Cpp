#pragma once

#include <string>
#include <vector>
#include <map>
#include <tuple>

// A simplified cross-margin account
class Account {
public:
    // Constructor with initial balance. We allow cross margin with negative wallet balance
    // as long as total equity >= 0.
    Account(double initial_balance, int vip_level = 0);

    double get_balance() const;      // Raw wallet balance (may be negative in cross margin)
    double total_unrealized_pnl() const;
    double get_equity() const;       // = balance_ + total_unrealized_pnl()

    // Set the leverage for a specific symbol
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    // Get the leverage for a specific symbol
    double get_symbol_leverage(const std::string& symbol) const;

    // Place a new order (long/short, market/limit). 
    // This will check total equity vs required margin+fee, 
    // and reduce balance_ by (margin+fee) allowing negative.
    void place_order(const std::string& symbol,
        double quantity,
        double price,
        bool is_long,
        bool is_market_order);

    // Update positions with new market data:
    //  - Recompute unrealized PNL
    //  - Check liquidation (full liquidation if equity < totalMaintenanceMargin)
    void update_positions(const std::map<std::string, double>& symbol_prices);

    // Close a position (fully). 
    // This realizes any PNL, refunds margin, subtracts fees, etc.
    void close_position(const std::string& symbol, double price, bool is_market_order);

private:
    struct Position {
        std::string symbol;
        double quantity;
        double entry_price;
        bool is_long;
        double unrealized_pnl;
        double notional;
        double initial_margin;
        double maintenance_margin;
        double fee;
        double leverage;
        double fee_rate;
    };

    double balance_;          // The wallet balance. Can be negative in cross margin
    double used_margin_;      // The sum of initial_margin across all positions (not strictly needed in cross, but kept)
    int vip_level_;           // For future VIP-based fees

    std::vector<Position> positions_;
    // Keep a map from symbol -> leverage
    std::map<std::string, double> symbol_leverage_;

    // Helper: obtain tier-based margin rates
    std::tuple<double, double> get_tier_info(double notional) const;

    // Helper: obtain maker/taker fee rates for the current VIP level
    std::tuple<double, double> get_fee_rates() const;

    // If a position for this symbol already exists, adjust margin usage after changing leverage
    bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);
};
