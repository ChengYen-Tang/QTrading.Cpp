#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <tuple>

/**
 * Hedge-mode Account class:
 * - Allows multiple positions (long/short) on the same symbol simultaneously.
 * - Distinguishes positions from different orders (one order -> one position, partial fill merges).
 * - Closing a position also goes through the matching engine by creating a "closing order".
 * - Market or Limit is determined by price <= 0 => market, > 0 => limit.
 */
class Account {
public:
    Account(double initial_balance, int vip_level = 0);

    double get_balance() const;
    double total_unrealized_pnl() const;
    double get_equity() const;

    // Leverage set/get
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    double get_symbol_leverage(const std::string& symbol) const;

    /**
     * place_order:
     * If price > 0 => limit order, else (price <= 0) => market order.
     */
    void place_order(const std::string& symbol,
        double quantity,
        double price,
        bool is_long);
    /**
     * market order.
     */
    void place_order(const std::string& symbol,
        double quantity,
        bool is_long);

    /**
     * The main matching function:
     * - For each open order:
     *   - If closing_position_id != -1 => offset that existing position
     *   - Else => normal opening order
     * - Support partial fills
     * - Recompute PnL, check liquidation
     */
    void update_positions(const std::map<std::string, std::pair<double, double>>& symbol_price_volume);

    /**
     * close_position(by symbol):
     * - If user provides price <= 0 => market close,
     *   else => limit close
     * - Internally creates "closing orders" for all positions under that symbol.
     */
    void close_position(const std::string& symbol, double price);
    /**
     * close_position(by symbol):
     * - Use market close
     * - Internally creates "closing orders" for all positions under that symbol.
     */
    void close_position(const std::string& symbol);

    /**
     * close_position_by_id:
     * - If price <= 0 => market close, else => limit close
     * - Creates a single "closing order" for a specific position
     */
    void close_position_by_id(int position_id, double price);
    /**
     * close_position_by_id:
     * - Use market close
     * - Creates a single "closing order" for a specific position
     */
    void close_position_by_id(int position_id);

    // Cancel a specific open order by ID (remaining part).
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

    // ------------------- Queries & Cancel -------------------
    const std::vector<Account::Order>& get_all_open_orders() const;
    const std::vector<Account::Position>& get_all_positions() const;

private:

    double balance_;
    double used_margin_;
    int    vip_level_;

    // Per-symbol leverage
    std::map<std::string, double> symbol_leverage_;

    // ID counters
    int next_order_id_;
    int next_position_id_;

    // All open orders
    std::vector<Order> open_orders_;

    // All active positions
    std::vector<Position> positions_;

    // For partial fills from same order => same position
    std::unordered_map<int, int> order_to_position_;

    // ------------------- Internal Helpers -------------------
    int  generate_order_id();
    int  generate_position_id();

    std::tuple<double, double> get_tier_info(double notional) const;
    std::tuple<double, double> get_fee_rates() const;
    bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    // Helper to place a "closing order" for position
    void place_closing_order(int position_id, double quantity, double price);
};
