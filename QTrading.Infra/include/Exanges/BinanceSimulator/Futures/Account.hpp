﻿#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

using namespace QTrading::dto;

/**
 * Simulated Binance Futures Account (supports one‑way / hedge mode)
 */
class Account {
public:
    Account(double initial_balance, int vip_level = 0);

    // Basic account information
    double get_balance() const;
    double total_unrealized_pnl() const;
    double get_equity() const;

    // Set/get trading mode (one‑way or hedge)
    void set_position_mode(bool hedgeMode);
    bool is_hedge_mode() const;

    // Set and get leverage for a given symbol
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    double get_symbol_leverage(const std::string& symbol) const;

    /**
     * place_order:
     *  - price > 0 => Limit Order
     *  - price <= 0 => Market Order
     *  - If reduce_only is true, this order is used for reducing positions only.
     *    In one‑way mode, this parameter is rarely used since reverse orders automatically reduce positions;
     *    In hedge mode, it clearly distinguishes whether the order is for increasing or reducing position.
     */
    void place_order(const std::string& symbol,
        double quantity,
        double price,
        bool is_long,
        bool reduce_only = false);

    // Overload: market order (price=0, reduce_only=false by default)
    void place_order(const std::string& symbol, double quantity, bool is_long, bool reduce_only = false);

    /**
     * update_positions:
     * Core matching and position updating logic.
     * symbol_price_volume: mapping from symbol to (market price, available volume)
     */
    void update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume);

    /**
     * close_position:
     *  - If price <= 0 => market close; if price > 0 => limit close.
     *
     *  In one‑way mode: close_position(symbol) directly closes all positions for that symbol.
     *  In hedge mode:
     *      - close_position(symbol, is_long) can close only long or short positions.
     *      - The version without direction can be customized (e.g., close both long and short) per requirements.
     */
    void close_position(const std::string& symbol, double price);
    void close_position(const std::string& symbol);

    // New: In hedge mode, specify whether to close long or short position.
    void close_position(const std::string& symbol, bool is_long, double price = 0.0);

    // Cancel an open order by its ID (cancels only the unfilled portion)
    void cancel_order_by_id(int order_id);

    // Query functions returning const references to avoid copying.
    const std::vector<Order>& get_all_open_orders() const;
    const std::vector<Position>& get_all_positions() const;

private:
    // Account funds
    double balance_;
    double used_margin_;
    int vip_level_;

    // One‑way / Hedge mode flag
    bool hedge_mode_;

    // Mapping from symbol to leverage
    std::unordered_map<std::string, double> symbol_leverage_;

    // ID counters
    int next_order_id_;
    int next_position_id_;

    // All open orders
    std::vector<Order> open_orders_;

    // All active positions
    std::vector<Position> positions_;

    // Mapping from order_id to position_id for merging partial fills from the same order.
    std::unordered_map<int, int> order_to_position_;

    // ------------------- Internal Helpers -------------------
    inline int generate_order_id();
    inline int generate_position_id();
    inline std::tuple<double, double> get_tier_info(double notional) const;
    inline std::tuple<double, double> get_fee_rates() const;
    inline bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    // Helper for generating a closing order for a given position.
    inline void place_closing_order(int position_id, double quantity, double price);

    // Helper to merge positions with the same (symbol, is_long) into one.
    inline void merge_positions();

    // --- New private helper functions for refactoring ---

    // Process reverse orders in one‑way mode.
    // Returns true if the reverse order was handled.
    inline bool handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, bool is_long);

    // Process a closing order fill: update the matching position based on the fill quantity
    // and add any remaining order quantity to the leftover orders.
    inline void processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    // Process a reduce_only opening order; returns true if processed.
    inline bool processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    // Process a normal (non‑reduce_only) opening order fill.
    inline void processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    // Process an opening order fill, calling either the reduce_only or normal order processing.
    inline void processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);
};
