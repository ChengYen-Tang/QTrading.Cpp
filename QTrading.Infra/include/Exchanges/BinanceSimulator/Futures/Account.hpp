#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Dto/Market/Binance/Kline.hpp"
#include "Dto/Account/BalanceSnapshot.hpp"
#include "Dto/Trading/Side.hpp"

using namespace QTrading::dto;

/// @brief Simulated Binance Futures account supporting one-way and hedge modes.
///        Manages balance, margin, orders, and positions.
class Account {
public:
    Account(double initial_balance, int vip_level = 0);

    QTrading::Dto::Account::BalanceSnapshot get_balance() const;

    double total_unrealized_pnl() const;
    double get_equity() const;

    double get_wallet_balance() const;
    double get_margin_balance() const;
    double get_available_balance() const;

    void set_position_mode(bool hedgeMode);
    bool is_hedge_mode() const;

    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    double get_symbol_leverage(const std::string& symbol) const;

    bool place_order(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false);

    bool place_order(const std::string& symbol,
        double quantity,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false);

    void update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume);
    void update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>& symbol_kline);

    void close_position(const std::string& symbol, double price);
    void close_position(const std::string& symbol);

    void close_position(const std::string& symbol,
        QTrading::Dto::Trading::PositionSide position_side,
        double price = 0.0);

    void cancel_order_by_id(int order_id);

    const std::vector<Order>& get_all_open_orders() const;
    const std::vector<Position>& get_all_positions() const;

private:
    double balance_;
    double wallet_balance_;
    double used_margin_;

    int vip_level_;
    bool hedge_mode_;

    std::unordered_map<std::string, double> symbol_leverage_;

    int next_order_id_;
    int next_position_id_;

    std::vector<Order> open_orders_;
    std::vector<Position> positions_;

    std::unordered_map<int, int> order_to_position_;

    inline int generate_order_id();
    inline int generate_position_id();

    inline std::tuple<double, double> get_tier_info(double notional) const;
    inline std::tuple<double, double> get_fee_rates() const;
    inline bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    inline void place_closing_order(int position_id, double quantity, double price);

    inline void merge_positions();

    inline bool handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, QTrading::Dto::Trading::OrderSide side);

    inline void processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    inline bool processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    inline void processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    inline void processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);
};
