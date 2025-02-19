#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>

/**
 * Optimized Hedge-mode Account class for high-frequency trading.
 * - Allows multiple positions (long/short) on the same symbol concurrently.
 * - Distinguishes positions by order; partial fills from the same order merge.
 * - Closing a position goes through the matching engine via "closing orders."
 * - Market order: price <= 0; Limit order: price > 0.
 */
class Account {
public:
    Account(double initial_balance, int vip_level = 0);

    // Basic getters
    double get_balance() const;
    double total_unrealized_pnl() const;
    double get_equity() const;

    // Leverage: set and get for a given symbol
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    double get_symbol_leverage(const std::string& symbol) const;

    /**
     * Place an order.
     * If price > 0, it's a limit order; if price <= 0, it's a market order.
     */
    void place_order(const std::string& symbol, double quantity, double price, bool is_long);
    // Overload for market order (price defaults to 0)
    void place_order(const std::string& symbol, double quantity, bool is_long);

    /**
     * Main matching function.
     * For each open order:
     *  - If closing_position_id != -1, process as a closing order.
     *  - Else, process as a normal opening order.
     * Supports partial fills, recalculates PnL, and checks for liquidation.
     *
     * The parameter symbol_price_volume uses unordered_map for O(1) lookups.
     */
    void update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume);

    /**
     * Close positions by symbol.
     * If price <= 0, market close; if price > 0, limit close.
     * Internally creates closing orders for all positions under that symbol.
     */
    void close_position(const std::string& symbol, double price);
    void close_position(const std::string& symbol);

    /**
     * Close a specific position by its ID.
     * If price <= 0, market close; if price > 0, limit close.
     * Creates a single closing order for the specified position.
     */
    void close_position_by_id(int position_id, double price);
    void close_position_by_id(int position_id);

    // Cancel an open order by its ID (cancels only the unfilled portion)
    void cancel_order_by_id(int order_id);

    // ------------------- Data Structures -------------------
    struct Order {
        int         id;               // unique order ID
        std::string symbol;
        double      quantity;         // remaining quantity to be matched
        double      price;            // <= 0 => market, > 0 => limit
        bool        is_long;

        // If >= 0 => means this order is closing an existing position_id
        // If -1 => normal opening order
        int         closing_position_id;
    };

    struct Position {
        int         id;             // unique position ID
        int         order_id;       // which order originally opened it
        std::string symbol;
        double      quantity;
        double      entry_price;
        bool        is_long;
        double      unrealized_pnl;
        double      notional;
        double      initial_margin;
        double      maintenance_margin;
        double      fee;
        double      leverage;
        double      fee_rate;
    };

    // Query functions returning const references to avoid copying.
    const std::vector<Order>& get_all_open_orders() const;
    const std::vector<Position>& get_all_positions() const;

private:
    double balance_;
    double used_margin_;
    int vip_level_;

    // Mapping from symbol to its leverage.
    std::unordered_map<std::string, double> symbol_leverage_;

    // ID counters.
    int next_order_id_;
    int next_position_id_;

    // All open orders.
    std::vector<Order> open_orders_;

    // All active positions.
    std::vector<Position> positions_;

    // Map for merging partial fills from the same order: order_id -> position_id.
    std::unordered_map<int, int> order_to_position_;

    // ------------------- Internal Helpers -------------------
    int generate_order_id();
    int generate_position_id();

    // Returns (maintenance_margin_rate, max_leverage) for a given notional.
    std::tuple<double, double> get_tier_info(double notional) const;
    // Returns (maker_fee_rate, taker_fee_rate) based on VIP level.
    std::tuple<double, double> get_fee_rates() const;
    // Adjust positions for a given symbol if leverage changes.
    bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    // Helper to place a closing order for a given position.
    void place_closing_order(int position_id, double quantity, double price);
};
