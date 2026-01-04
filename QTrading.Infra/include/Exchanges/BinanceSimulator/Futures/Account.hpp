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
#include <optional>
#include <functional>

using namespace QTrading::dto;

/// @brief Simulated Binance Futures account supporting one-way and hedge modes.
///        Manages balance, margin, orders, and positions.
class Account {
public:
    enum class KlineVolumeSplitMode {
        LegacyTotalOnly = 0,
        TakerBuyOnly = 1,
        TakerBuyOrHeuristic = 2,
    };

    // --- Policies (P2.1) ---
    using FeeRates = std::tuple<double, double>; // (maker, taker)

    struct Policies {
        // Fee rates by VIP level
        std::function<FeeRates(int vip_level)> fee_rates;

        // Decide if order can fill on this kline and whether it's taker
        std::function<std::pair<bool, bool>(const Order& ord, const QTrading::Dto::Market::Binance::KlineDto& k)> can_fill_and_taker;

        // Directional liquidity split: returns {has_dir_liq, {buy_liq, sell_liq}}
        std::function<std::pair<bool, std::pair<double, double>>(
            KlineVolumeSplitMode mode,
            const QTrading::Dto::Market::Binance::KlineDto& k)> directional_liquidity;

        // Execution price given slippage settings
        std::function<double(const Order& ord,
            const QTrading::Dto::Market::Binance::KlineDto& k,
            double market_exec_slip,
            double limit_exec_slip)> execution_price;

        // Liquidation execution price (defaults to Low/High)
        std::function<double(const Position& pos, const QTrading::Dto::Market::Binance::KlineDto& k)> liquidation_price;
    };

    static Policies DefaultPolicies();

    Account(double initial_balance, int vip_level = 0);
    Account(double initial_balance, int vip_level, Policies policies);

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

    void set_market_slippage_buffer(double pct);

    // Kline-based market execution slippage (fraction of price, e.g. 0.001 = 0.1%).
    // Applied to market fills in update_positions.
    void set_market_execution_slippage(double pct);

    // Kline-based limit execution slippage (fraction of price).
    // Applied to triggered limit fills to model worse price within the candle while preserving limit protection.
    void set_limit_execution_slippage(double pct);

    void set_kline_volume_split_mode(KlineVolumeSplitMode mode);

    void set_enable_console_output(bool enable);
    bool is_console_output_enabled() const;

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

    // Fast lookup indices (rebuilt when containers are rebuilt).
    std::unordered_map<int, size_t> open_order_index_by_id_;
    std::unordered_map<int, size_t> position_index_by_id_;

    // Per-tick reusable scratch buffers to reduce allocations in update_positions().
    std::unordered_map<std::string, double> remaining_vol_;
    std::unordered_map<std::string, std::pair<double, double>> remaining_liq_;
    std::unordered_map<std::string, bool> has_dir_liq_;
    std::unordered_map<std::string, std::vector<size_t>> per_symbol_;
    std::vector<bool> keep_open_order_;
    std::vector<size_t> fillable_;
    std::vector<Order> next_open_orders_;
    std::vector<Order> single_leftover_;

    // Last known mark/close price per symbol (from kline ClosePrice). Used for market-order notional estimation.
    std::unordered_map<std::string, double> last_mark_price_;

    // For market orders, notional is estimated as qty * mark * (1 + buffer).
    double market_slippage_buffer_{ 0.005 };

    // For market orders, fill price is pessimistically adjusted using kline OHLC.
    // buy: min(High, Close*(1+slip)), sell: max(Low, Close*(1-slip))
    double market_execution_slippage_{ 0.0 };

    // For limit orders that trigger, optionally fill at a worse price within OHLC.
    // buy limit: fill = min(limit, close*(1+slip), high)
    // sell limit: fill = max(limit, close*(1-slip), low)
    double limit_execution_slippage_{ 0.0 };

    Policies policies_{ };

    KlineVolumeSplitMode kline_volume_split_mode_{ KlineVolumeSplitMode::TakerBuyOnly };

    bool enable_console_output_{ false };

    // Monotonic state version for O(1) change detection by exchange.
    uint64_t state_version_{ 0 };

    int generate_order_id();
    int generate_position_id();

    std::tuple<double, double> get_tier_info(double notional) const;
    std::tuple<double, double> get_fee_rates() const;
    bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    void place_closing_order(int position_id, double quantity, double price);

    void merge_positions();

    bool handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, QTrading::Dto::Trading::OrderSide side);

    void processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    bool processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    void processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    void processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    void rebuild_open_order_index_();
    void rebuild_position_index_();

public:
    uint64_t get_state_version() const noexcept { return state_version_; }
};
