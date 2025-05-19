#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

using namespace QTrading::dto;

/// @brief Simulated Binance Futures account supporting one-way and hedge modes.
///        Manages balance, margin, orders, and positions.
class Account {
public:
    /// @brief Construct an Account with initial balance and VIP level.
    /// @param initial_balance Starting cash balance.
    /// @param vip_level      VIP fee tier (0–9).
    Account(double initial_balance, int vip_level = 0);

    /// @brief Get current cash balance.
    /// @return Available balance (after realized PnL minus margin).
    double get_balance() const;
    /// @brief Compute total unrealized PnL across all positions.
    /// @return Sum of unrealized PnL.
    double total_unrealized_pnl() const;
    /// @brief Get total equity (balance + unrealized PnL).
    /// @return Current account equity.
    double get_equity() const;

    /// @brief Set trading mode: one-way (false) or hedge (true).
    ///        Cannot switch if positions are open.
    /// @param hedgeMode True for hedge mode, false for one-way mode.
    void set_position_mode(bool hedgeMode);
    /// @brief Query if account is in hedge mode.
    /// @return True if hedge mode, false if one-way.
    bool is_hedge_mode() const;

    /// @brief Set leverage factor for a specific symbol.
    /// @param symbol      Trading symbol.
    /// @param newLeverage Desired leverage (>0).
    /// @throw std::runtime_error if newLeverage ≤ 0 or insufficient equity.
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    /// @brief Get current leverage for a symbol.
    /// @param symbol Trading symbol.
    /// @return Leverage factor (≥1).
    double get_symbol_leverage(const std::string& symbol) const;

    /// @brief Place a new order.
    /// @param symbol     Trading symbol.
    /// @param quantity   Order quantity.
    /// @param price      Limit price (>0) or market (≤0).
    /// @param is_long    True for buy/long, false for sell/short.
    /// @param reduce_only If true, only reduce an existing position.
    void place_order(const std::string& symbol,
        double quantity,
        double price,
        bool is_long,
        bool reduce_only = false);

    /// @brief Overload: market order (price=0).
    /// @param symbol     Trading symbol.
    /// @param quantity   Order quantity.
    /// @param is_long    True for long, false for short.
    /// @param reduce_only If true, reduce-only.
    void place_order(const std::string& symbol, double quantity, bool is_long, bool reduce_only = false);

    /// @brief Core matching and position update logic.
    /// @param symbol_price_volume Map from symbol to (market price, available volume).
    void update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume);


    /// @brief Close position(s) for a symbol at given price.
    ///        In one-way mode: closes all. In hedge mode: customizable.
    /// @param symbol Trading symbol.
    /// @param price  Limit price (>0) or market (≤0).
    void close_position(const std::string& symbol, double price);
    /// @brief Close all positions for a symbol at market price.
    /// @param symbol Trading symbol.
    void close_position(const std::string& symbol);

    /// @brief In hedge mode, close only one side (long/short) at price.
    /// @param symbol Trading symbol.
    /// @param is_long True to close long side, false to close short.
    /// @param price   Limit price (>0) or market (≤0).
    void close_position(const std::string& symbol, bool is_long, double price = 0.0);


    /// @brief Cancel an open order by its ID (removes unfilled remainder).
    /// @param order_id Unique identifier of the order.
    void cancel_order_by_id(int order_id);

    /// @brief Get all open orders.
    /// @return Const reference to vector of Order DTOs.
    const std::vector<Order>& get_all_open_orders() const;
    /// @brief Get all active positions.
    /// @return Const reference to vector of Position DTOs.
    const std::vector<Position>& get_all_positions() const;

private:
    ///< Available cash balance.
    double balance_;
    ///< Margin currently in use.
    double used_margin_;
    ///< VIP level for fee calculation.
    int vip_level_;

    ///< Hedge mode flag (true) or one-way (false).
    bool hedge_mode_;

    ///< Per-symbol leverage.
    std::unordered_map<std::string, double> symbol_leverage_;

    ///< Counter for generating unique order IDs.
    int next_order_id_;
    ///< Counter for generating unique position IDs.
    int next_position_id_;

    ///< Pending open orders.
    std::vector<Order> open_orders_;
    ///< Active positions.
    std::vector<Position> positions_;

    ///< Map from order ID to position ID.
    std::unordered_map<int, int> order_to_position_;

    /// @brief Generate a new unique order ID.
    inline int generate_order_id();
    /// @brief Generate a new unique position ID.
    inline int generate_position_id();

    /// @brief Get maintenance margin rate and max leverage for a notional.
    /// @param notional Total position notional.
    /// @return Tuple of (maintenance_margin_rate, max_leverage).
    inline std::tuple<double, double> get_tier_info(double notional) const;
    /// @brief Get current maker/taker fee rates based on VIP level.
    /// @return Tuple of (maker_fee_rate, taker_fee_rate).
    inline std::tuple<double, double> get_fee_rates() const;
    /// @brief Adjust existing positions' leverage for a symbol.
    /// @param symbol  Trading symbol.
    /// @param oldLev  Current leverage.
    /// @param newLev  Desired leverage.
    /// @return True if adjustment succeeded; false if insufficient equity.
    inline bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    /// @brief Create a closing order for an existing position.
    /// @param position_id ID of position to close.
    /// @param quantity    Quantity to close.
    /// @param price       Limit price (>0) or market (≤0).
    inline void place_closing_order(int position_id, double quantity, double price);

    /// @brief Merge positions of same symbol and direction into one.
    inline void merge_positions();

    /// @brief Handle reverse orders in one-way mode (auto-reduce).
    /// @param symbol   Trading symbol.
    /// @param quantity Order quantity.
    /// @param price    Order price.
    /// @param is_long  Desired direction.
    /// @return True if reverse logic consumed the order.
    inline bool handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, bool is_long);

    /// @brief Process a closing order fill and update PnL and margin.
    inline void processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    /// @brief Process a reduce-only opening order fill.
    /// @return True if it matched an existing position.
    inline bool processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    /// @brief Process a normal opening order fill (new position or increase).
    inline void processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    /// @brief Dispatch opening order fill to the appropriate handler.
    inline void processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);
};
