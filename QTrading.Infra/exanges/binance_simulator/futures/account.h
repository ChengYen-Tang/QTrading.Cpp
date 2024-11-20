// account.h
#pragma once

#include <string>
#include <vector>
#include "market_data.h"
#include "config.h"

class Account {
public:
    Account(double initial_balance, int vip_level = 0);

    void set_leverage(double lev);
    double get_balance() const;

    void place_order(const std::string& symbol, double quantity, double price, bool is_long, bool is_market_order);
    void update_positions(const MarketData::Kline& kline);
    void close_position(const std::string& symbol, double price, bool is_market_order);

    double total_unrealized_pnl() const;

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

    double balance;
    double used_margin;
    double leverage;
    double max_leverage_allowed;
    int vip_level;

    std::vector<Position> positions;

    std::tuple<double, double, double> get_margin_rates(double notional) const;
    std::tuple<double, double> get_fee_rates() const;
};
