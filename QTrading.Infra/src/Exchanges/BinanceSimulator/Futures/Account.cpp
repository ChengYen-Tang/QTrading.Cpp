#include "Exchanges/BinanceSimulator/Futures/Config.hpp"
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include "Exchanges/BinanceSimulator/Futures/AccountPolicies.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <future>
#include <thread>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace {

struct FeeModel {
    double maker_fee{};
    double taker_fee{};

    explicit FeeModel(const std::tuple<double, double>& fees)
    {
        maker_fee = std::get<0>(fees);
        taker_fee = std::get<1>(fees);
    }

    double fee_rate(bool is_taker) const noexcept { return is_taker ? taker_fee : maker_fee; }
};

struct PriceExecutionModel {
    double market_exec_slippage{};
    double limit_exec_slippage{};

    double execution_price(const Order& ord, const KlineDto& k) const
    {
        const bool is_market = (ord.price <= 0.0);
        double fill_price = is_market ? k.ClosePrice : ord.price;

        if (is_market) {
            const double slip = std::max(0.0, market_exec_slippage);
            if (slip > 0.0) {
                if (ord.side == OrderSide::Buy) {
                    fill_price = std::min(k.HighPrice, k.ClosePrice * (1.0 + slip));
                }
                else {
                    fill_price = std::max(k.LowPrice, k.ClosePrice * (1.0 - slip));
                }
            }
        }
        else {
            const double slip = std::max(0.0, limit_exec_slippage);
            if (slip > 0.0) {
                if (ord.side == OrderSide::Buy) {
                    const double worse = std::min(k.HighPrice, k.ClosePrice * (1.0 + slip));
                    fill_price = std::min(ord.price, worse);
                }
                else {
                    const double worse = std::max(k.LowPrice, k.ClosePrice * (1.0 - slip));
                    fill_price = std::max(ord.price, worse);
                }
            }
        }

        return fill_price;
    }
};

struct FillModel {
    Account::KlineVolumeSplitMode split_mode{ Account::KlineVolumeSplitMode::LegacyTotalOnly };

    std::pair<bool, bool> can_fill_and_taker(const Order& ord, const KlineDto& k) const
    {
        const bool is_market = (ord.price <= 0.0);
        if (is_market) {
            return { true, true };
        }

        const bool is_buy = (ord.side == OrderSide::Buy);
        const bool triggered = (is_buy ? (k.LowPrice <= ord.price) : (k.HighPrice >= ord.price));
        if (!triggered) {
            return { false, false };
        }

        const bool marketable_at_close = (is_buy ? (k.ClosePrice <= ord.price) : (k.ClosePrice >= ord.price));
        return { true, marketable_at_close };
    }

    std::pair<bool, std::pair<double, double>> build_directional_liquidity(const KlineDto& k) const
    {
        const double vol = std::max(0.0, k.Volume);
        if (split_mode == Account::KlineVolumeSplitMode::LegacyTotalOnly || vol <= 0.0) {
            return { false, {0.0, 0.0} };
        }

        bool has = false;
        double buy_liq = 0.0;
        double sell_liq = 0.0;

        if (k.TakerBuyBaseVolume > 0.0) {
            has = true;
            buy_liq = std::clamp(k.TakerBuyBaseVolume, 0.0, vol);
            sell_liq = vol - buy_liq;
        }
        else if (split_mode == Account::KlineVolumeSplitMode::TakerBuyOrHeuristic) {
            const double range_raw = k.HighPrice - k.LowPrice;
            if (std::abs(range_raw) < 1e-12) {
                buy_liq = vol * 0.5;
            }
            else {
                const double range = std::max(1e-12, range_raw);
                double close_loc = (k.ClosePrice - k.LowPrice) / range;
                close_loc = std::clamp(close_loc, 0.0, 1.0);
                buy_liq = vol * close_loc;
            }
            sell_liq = vol - buy_liq;
            has = true;
        }

        return { has, {buy_liq, sell_liq} };
    }
};

static bool order_closes_position(const Order& ord, const Position& pos, bool hedge_mode)
{
    if (pos.symbol != ord.symbol) return false;

    const bool is_buy = (ord.side == OrderSide::Buy);
    const bool close_dir_ok = (pos.is_long && !is_buy) || (!pos.is_long && is_buy);
    if (!close_dir_ok) return false;

    if (!hedge_mode) {
        // One-way: only one net position, any opposite-side action reduces.
        return true;
    }

    // Hedge: must target correct side.
    if (ord.position_side == PositionSide::Both) return false;
    const bool target_long = (ord.position_side == PositionSide::Long);
    return target_long == pos.is_long;
}

static bool has_reducible_position(const std::vector<Position>& positions, const Order& ord, bool hedge_mode)
{
    for (const auto& p : positions) {
        if (order_closes_position(ord, p, hedge_mode)) return true;
    }
    return false;
}

static bool order_opens_in_hedge_mode(const Order& o)
{
    const bool is_buy = (o.side == OrderSide::Buy);
    // Long opens with BUY, Short opens with SELL.
    if ((o.position_side == PositionSide::Long && is_buy) ||
        (o.position_side == PositionSide::Short && !is_buy)) {
        return true;
    }
    return false;
}

static bool order_reserves_open_margin(const Order& o)
{
    // Do not reserve for explicit closing orders or reduce-only orders.
    if (o.closing_position_id >= 0) return false;
    if (o.reduce_only) return false;

    // One-way orders use PositionSide::Both.
    if (o.position_side == PositionSide::Both) {
        // With flip splitting, any remaining opening order can increase exposure.
        return true;
    }

    // Hedge: reserve only for opening-direction orders.
    return order_opens_in_hedge_mode(o);
}

} // namespace

/// @brief Simulated Binance Futures Account implementation.
/// @details Supports one-way and hedge modes, order matching, margin, fees, and auto-liquidation.
Account::Account(double initial_balance, int vip_level)
    : wallet_balance_(initial_balance),
    balance_(initial_balance),
    used_margin_(0.0),
    vip_level_(vip_level),
    hedge_mode_(false),
    next_order_id_(1),
    next_position_id_(1),
    policies_(DefaultPolicies()),
    tick_memory_(std::pmr::new_delete_resource())
{
    open_orders_.reserve(1024);
    positions_.reserve(1024);

    open_order_index_by_id_.reserve(2048);
    position_index_by_id_.reserve(2048);
    position_indices_by_symbol_.reserve(1024);

    symbol_id_by_name_.reserve(1024);
    remaining_vol_.reserve(1024);
    remaining_liq_.reserve(1024);
    has_dir_liq_.reserve(1024);
    per_symbol_.reserve(1024);
    per_symbol_active_ids_.reserve(1024);
    kline_by_id_.reserve(1024);
    merge_indices_.reserve(1024);
    merged_positions_.reserve(1024);
}

Account::Account(double initial_balance, int vip_level, Policies policies)
    : Account(initial_balance, vip_level)
{
    policies_ = std::move(policies);
}

void Account::set_enable_console_output(bool enable)
{
    enable_console_output_ = enable;
}

bool Account::is_console_output_enabled() const
{
    return enable_console_output_;
}

void Account::set_max_match_orders_per_symbol(size_t limit)
{
    max_match_orders_per_symbol_ = limit;
}

size_t Account::max_match_orders_per_symbol() const
{
    return max_match_orders_per_symbol_;
}

static QTrading::Dto::Account::BalanceSnapshot build_snapshot(
    double wallet_balance,
    const std::vector<Position>& positions,
    const std::vector<Order>& open_orders,
    const std::unordered_map<std::string, double>& symbol_leverage,
    const std::unordered_map<std::string, double>& last_mark_price,
    double market_slippage_buffer)
{
    QTrading::Dto::Account::BalanceSnapshot s;
    s.WalletBalance = wallet_balance;

    double unreal = 0.0;
    double posInit = 0.0;
    double maint = 0.0;
    for (const auto& p : positions) {
        unreal += p.unrealized_pnl;
        posInit += p.initial_margin;
        maint += p.maintenance_margin;
    }

    s.UnrealizedPnl = unreal;
    s.MarginBalance = s.WalletBalance + s.UnrealizedPnl;

    s.PositionInitialMargin = posInit;
    s.MaintenanceMargin = maint;

    auto estimate_notional = [&](const Order& o) -> double {
        if (o.quantity <= 0.0) return 0.0;
        if (o.price > 0.0) return o.quantity * o.price;
        auto it = last_mark_price.find(o.symbol);
        if (it == last_mark_price.end()) return 0.0;
        return o.quantity * it->second * (1.0 + std::max(0.0, market_slippage_buffer));
    };

    auto get_lev = [&](const Order& o) -> double {
        auto it = symbol_leverage.find(o.symbol);
        double lev = (it != symbol_leverage.end()) ? it->second : 1.0;
        return std::max(1.0, lev);
    };

    // Exposure-based open order initial margin:
    // - closing orders: no
    // - reduce_only: no (can't increase exposure)
    // - one-way: only orders that would increase exposure reserve margin
    // - hedge: only opening-direction orders reserve margin
    double openOrdInit = 0.0;
    for (const auto& o : open_orders) {
        if (!order_reserves_open_margin(o)) continue;

        const double notional = estimate_notional(o);
        if (notional <= 0.0) continue;
        openOrdInit += notional / get_lev(o);
    }

    s.OpenOrderInitialMargin = openOrdInit;

    s.AvailableBalance = s.MarginBalance - s.PositionInitialMargin - s.OpenOrderInitialMargin;
    s.Equity = s.MarginBalance;
    return s;
}

QTrading::Dto::Account::BalanceSnapshot Account::get_balance() const {
    if (balance_cache_version_ != balance_version_) {
        balance_cache_ = build_snapshot(wallet_balance_, positions_, open_orders_, symbol_leverage_, last_mark_price_, market_slippage_buffer_);
        balance_cache_version_ = balance_version_;
    }
    return balance_cache_;
}

void Account::set_market_slippage_buffer(double pct)
{
    market_slippage_buffer_ = pct;
    mark_balance_dirty_();
}

void Account::set_market_execution_slippage(double pct)
{
    market_execution_slippage_ = pct;
}

void Account::set_limit_execution_slippage(double pct)
{
    limit_execution_slippage_ = pct;
}

void Account::set_kline_volume_split_mode(KlineVolumeSplitMode mode)
{
    kline_volume_split_mode_ = mode;
}

void Account::mark_open_orders_dirty_()
{
    ++open_orders_version_;
    mark_balance_dirty_();
}

void Account::mark_balance_dirty_()
{
    ++balance_version_;
}

void Account::ensure_symbol_capacity_(size_t id)
{
    const size_t need = id + 1;
    if (per_symbol_.size() < need) {
        per_symbol_.resize(need);
        remaining_vol_.resize(need);
        remaining_liq_.resize(need);
        has_dir_liq_.resize(need);
        kline_by_id_.resize(need);
    }
}

size_t Account::get_symbol_id_(const std::string& symbol)
{
    auto it = symbol_id_by_name_.find(symbol);
    if (it != symbol_id_by_name_.end()) {
        return it->second;
    }

    const size_t id = symbol_id_by_name_.size();
    symbol_id_by_name_.emplace(symbol, id);
    ensure_symbol_capacity_(id);
    return id;
}

void Account::rebuild_per_symbol_cache_()
{
    for (auto& indices : per_symbol_) {
        indices.clear();
    }
    per_symbol_active_ids_.clear();
    per_symbol_active_ids_.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        const size_t sym_id = get_symbol_id_(open_orders_[i].symbol);
        auto& indices = per_symbol_[sym_id];
        if (indices.empty()) {
            per_symbol_active_ids_.push_back(sym_id);
        }
        indices.push_back(i);
    }

    auto less = [this](size_t a, size_t b) {
        const Order& A = open_orders_[a];
        const Order& B = open_orders_[b];

        const bool Am = (A.price <= 0.0);
        const bool Bm = (B.price <= 0.0);
        if (Am != Bm) return Am;

        const bool A_is_buy = (A.side == OrderSide::Buy);
        const bool B_is_buy = (B.side == OrderSide::Buy);

        if (!Am && A_is_buy == B_is_buy) {
            if (A_is_buy) {
                if (A.price != B.price) return A.price > B.price;
            }
            else {
                if (A.price != B.price) return A.price < B.price;
            }
        }

        return A.id < B.id;
    };

    for (size_t sym_id : per_symbol_active_ids_) {
        auto& indices = per_symbol_[sym_id];
        if (indices.size() > 1) {
            std::sort(indices.begin(), indices.end(), less);
        }
    }

    per_symbol_cache_version_ = open_orders_version_;
}

double Account::get_wallet_balance() const {
    return wallet_balance_;
}

double Account::get_margin_balance() const {
    return get_balance().MarginBalance;
}

double Account::get_available_balance() const {
    return get_balance().AvailableBalance;
}

double Account::total_unrealized_pnl() const {
    return get_balance().UnrealizedPnl;
}

double Account::get_equity() const {
    return get_balance().MarginBalance;
}

/// @brief Switch between one-way mode and hedge mode.
/// @param hedgeMode true to enable hedge mode (separate long/short), false for one-way.
/// @details Fails if any positions are currently open.
void Account::set_position_mode(bool hedgeMode) {
    // Disallow switching mode if there are open positions.
    if (!positions_.empty()) {
        if (enable_console_output_) {
            std::cerr << "[set_position_mode] Cannot switch mode while positions are open.\n";
        }
        return;
    }
    if (hedge_mode_ == hedgeMode) {
        return;
    }
    hedge_mode_ = hedgeMode;
    mark_balance_dirty_();
}


/// @brief Check whether hedge mode is enabled.
/// @return True if hedge mode; false for one-way.
bool Account::is_hedge_mode() const {
    return hedge_mode_;
}

/// @brief Get the current leverage for a symbol.
/// @param symbol Trading symbol.
/// @return Leverage multiplier (default 1.0).
double Account::get_symbol_leverage(const std::string& symbol) const {
    auto it = symbol_leverage_.find(symbol);
    return (it != symbol_leverage_.end()) ? it->second : 1.0;
}

/// @brief Set leverage for a symbol, adjusting existing positions if needed.
/// @param symbol       Trading symbol.
/// @param newLeverage  Desired leverage (>0).
/// @throws std::runtime_error if newLeverage <= 0.
void Account::set_symbol_leverage(const std::string& symbol, double newLeverage) {
    if (newLeverage <= 0)
        throw std::runtime_error("Leverage must be > 0.");
    double oldLev = 1.0;
    auto it = symbol_leverage_.find(symbol);
    if (it != symbol_leverage_.end()) {
        oldLev = it->second;
    }
    if (it == symbol_leverage_.end()) {
        symbol_leverage_[symbol] = newLeverage;
        mark_balance_dirty_();
    }
    else {
        if (adjust_position_leverage(symbol, oldLev, newLeverage)) {
            it->second = newLeverage;
        }
        else {
            if (enable_console_output_) {
                std::cerr << "[set_symbol_leverage] Not enough equity to adjust.\n";
            }
        }
    }
}

/// @brief Generate a unique order ID.
/// @return New order ID.
int Account::generate_order_id() {
    return next_order_id_++;
}


/// @brief Generate a unique position ID.
/// @return New position ID.
int Account::generate_position_id() {
    return next_position_id_++;
}


/// @brief Place an order (limit or market) into the account.
/// @param symbol       Trading symbol.
/// @param quantity     Amount to trade (>0).
/// @param price        Limit price (>0) or market (<=0).
/// @param is_long      true = long; false = short.
/// @param reduce_only  If true, only reduce existing positions.
bool Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only)
{
    if (quantity <= 0) {
        if (enable_console_output_) {
            std::cerr << "[place_order] Invalid quantity <= 0\n";
        }
        return false;
    }

    if (!hedge_mode_ && position_side != PositionSide::Both) {
        position_side = PositionSide::Both;
    }

    // Binance-like: in hedge mode the caller must specify Long/Short (no inference).
    if (hedge_mode_ && position_side == PositionSide::Both) {
        if (enable_console_output_) {
            std::cerr << "[place_order] Hedge-mode order must specify position_side (Long/Short).\n";
        }
        return false;
    }

    // In one-way mode, attempt to process reverse (flip) orders.
    if (!hedge_mode_) {
        if (handleOneWayReverseOrder(symbol, quantity, price, side))
            return true;
    }

    // A: reduceOnly reject policy on placement (do not add to open_orders_).
    // C: no exceptions; just return.
    if (reduce_only) {
        Order check{
            -1,
            symbol,
            quantity,
            price,
            side,
            position_side,
            true,
            -1
        };

        if (!has_reducible_position(positions_, check, hedge_mode_)) {
            if (enable_console_output_) {
                std::cerr << "[place_order] reduce_only rejected: no reducible position.\n";
            }
            return false;
        }
    }

    int oid = generate_order_id();
    Order newOrd{
        oid,
        symbol,
        quantity,
        price,
        side,
        position_side,
        reduce_only,
        -1
    };
    open_orders_.push_back(newOrd);
    mark_open_orders_dirty_();
    ++state_version_;
    return true;
}

bool Account::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only)
{
    return place_order(symbol, quantity, 0.0, side, position_side, reduce_only);
}

/// @brief Handle a reverse order in one-way mode (auto-reduce or reverse).
/// @param symbol    Trading symbol.
/// @param quantity  Order quantity.
/// @param price     Order price.
/// @param is_long   Direction of the new order.
/// @return true if the order was converted into a closing/reverse order.
bool Account::handleOneWayReverseOrder(const std::string& symbol, double quantity, double price, OrderSide side) {
    // In one-way mode we maintain at most one net position per symbol.
    auto it = position_indices_by_symbol_.find(symbol);
    if (it == position_indices_by_symbol_.end() || it->second.empty()) {
        return false;
    }

    Position& pos = positions_[it->second.front()];
    const bool posIsLong = pos.is_long;
    const bool orderIsBuy = (side == OrderSide::Buy);

    // Same-direction add: BUY with long position, or SELL with short position.
    if ((posIsLong && orderIsBuy) || (!posIsLong && !orderIsBuy)) {
        return false;
    }

    // Reverse-direction: this is a reduce/flip.
    double pos_qty = pos.quantity;
    double order_qty = quantity;

    // This is a closing action, so create a closing order targeting the existing position id.
    // Closing direction is opposite the position: long closes via SELL, short closes via BUY.
    const OrderSide closeSide = posIsLong ? OrderSide::Sell : OrderSide::Buy;

    if (order_qty <= pos_qty) {
        int oid = generate_order_id();
        Order closingOrd{
            oid,
            symbol,
            order_qty,
            price,
            closeSide,
            PositionSide::Both,
            false,
            pos.id
        };
        open_orders_.push_back(closingOrd);
        mark_open_orders_dirty_();
        return true;
    }

    // order_qty > pos_qty: first close the existing position, then open a new reverse position.
    {
        int oid = generate_order_id();
        Order closingOrd{
            oid,
            symbol,
            pos_qty,
            price,
            closeSide,
            PositionSide::Both,
            false,
            pos.id
        };
        open_orders_.push_back(closingOrd);
    }

    const double newOpenQty = order_qty - pos_qty;
    const OrderSide openSide = side;
    {
        int openOid = generate_order_id();
        Order newOpen{
            openOid,
            symbol,
            newOpenQty,
            price,
            openSide,
            PositionSide::Both,
            false,
            -1
        };
        open_orders_.push_back(newOpen);
    }
    mark_open_orders_dirty_();
    return true;
}

/// @brief Create a closing order for a specific position.
/// @param position_id  ID of the position to close.
/// @param quantity     Amount to close.
/// @param price        Close price (<=0 = market).
void Account::place_closing_order(int position_id, double quantity, double price) {
    auto it = position_index_by_id_.find(position_id);
    if (it != position_index_by_id_.end()) {
        const Position& pos = positions_[it->second];
        int oid = generate_order_id();
        const OrderSide closeSide = pos.is_long ? OrderSide::Sell : OrderSide::Buy;
        Order closingOrd{
            oid,
            pos.symbol,
            quantity,
            price,
            closeSide,
            hedge_mode_ ? (pos.is_long ? PositionSide::Long : PositionSide::Short) : PositionSide::Both,
            false,
            position_id
        };
        open_orders_.push_back(closingOrd);
        mark_open_orders_dirty_();
        return;
    }
    if (enable_console_output_) {
        std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
    }
}


/// @brief Merge positions of the same symbol & direction into one.
/// @details Aggregates quantities and recalculates weighted entry price, margin, fees.
void Account::merge_positions() {
    if (positions_.empty()) return;
    merge_indices_.clear();
    merge_indices_.reserve(positions_.size());
    for (size_t i = 0; i < positions_.size(); ++i) {
        merge_indices_.push_back(i);
    }

    auto less = [this](size_t a, size_t b) {
        const Position& A = positions_[a];
        const Position& B = positions_[b];
        if (A.symbol != B.symbol) return A.symbol < B.symbol;
        if (A.is_long != B.is_long) return A.is_long < B.is_long;
        return a < b;
    };
    std::sort(merge_indices_.begin(), merge_indices_.end(), less);

    merged_positions_.clear();
    merged_positions_.reserve(positions_.size());

    size_t i = 0;
    while (i < merge_indices_.size()) {
        const Position& first = positions_[merge_indices_[i]];
        Position merged = first;

        size_t j = i + 1;
        for (; j < merge_indices_.size(); ++j) {
            const Position& pos = positions_[merge_indices_[j]];
            if (pos.symbol != merged.symbol || pos.is_long != merged.is_long) break;

            double totalQty = merged.quantity + pos.quantity;
            if (totalQty < 1e-8) {
                merged.quantity = 0.0;
                continue;
            }
            double weightedPrice = (merged.entry_price * merged.quantity + pos.entry_price * pos.quantity) / totalQty;
            merged.quantity = totalQty;
            merged.entry_price = weightedPrice;
            merged.notional += pos.notional;
            merged.initial_margin += pos.initial_margin;
            merged.maintenance_margin += pos.maintenance_margin;
            merged.fee += pos.fee;
        }

        if (merged.quantity > 1e-8) {
            merged_positions_.push_back(merged);
        }
        i = j;
    }

    positions_.swap(merged_positions_);
    mark_balance_dirty_();
}

void Account::update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume) {
    // Backward-compatible adapter: treat provided price as ClosePrice and use it also as High/Low.
    std::unordered_map<std::string, KlineDto> kl;
    kl.reserve(symbol_price_volume.size());
    for (const auto& kv : symbol_price_volume) {
        KlineDto k;
        k.OpenPrice = kv.second.first;
        k.HighPrice = kv.second.first;
        k.LowPrice = kv.second.first;
        k.ClosePrice = kv.second.first;
        k.Volume = kv.second.second;
        kl.emplace(kv.first, k);
    }
    update_positions(kl);
}

void Account::update_positions(const std::unordered_map<std::string, KlineDto>& symbol_kline) {
    bool dirty = false;
    bool open_orders_changed = false;
    // Reset per-tick scratch allocator before building fill buffers.
    tick_memory_.release();
    fill_events_.clear();
    std::pmr::vector<unsigned char> keep_open_order{ &tick_memory_ };
    std::pmr::vector<Order> next_open_orders{ &tick_memory_ };
    std::vector<Order> single_leftover;
    single_leftover.reserve(1);

    // Cache mark prices for margin estimation.
    for (const auto& kv : symbol_kline) {
        last_mark_price_[kv.first] = kv.second.ClosePrice;
    }
    if (!symbol_kline.empty()) {
        mark_balance_dirty_();
    }

    const FeeModel fee_model(get_fee_rates());
    const FillModel fill_model{ kline_volume_split_mode_ };

    if (!kline_by_id_.empty()) {
        std::fill(kline_by_id_.begin(), kline_by_id_.end(), nullptr);
    }

    for (const auto& kv : symbol_kline) {
        const size_t sym_id = get_symbol_id_(kv.first);
        const auto& k = kv.second;
        kline_by_id_[sym_id] = &k;
        remaining_vol_[sym_id] = k.Volume;
        const auto [has, liq] = policies_.directional_liquidity
            ? policies_.directional_liquidity(kline_volume_split_mode_, k)
            : fill_model.build_directional_liquidity(k);
        has_dir_liq_[sym_id] = has ? 1 : 0;
        remaining_liq_[sym_id] = liq;
    }

    if (per_symbol_cache_version_ != open_orders_version_) {
        rebuild_per_symbol_cache_();
    }

    keep_open_order.assign(open_orders_.size(), true);

    struct SymbolWork {
        size_t sym_id;
        const std::vector<size_t>* indices;
    };
    struct FillPlanEntry {
        size_t idx;
        double fill_qty;
        double fill_price;
        bool is_taker;
    };
    struct SymbolPlan {
        std::vector<FillPlanEntry> fills;
        double remaining_vol{ 0.0 };
        double remaining_buy_liq{ 0.0 };
        double remaining_sell_liq{ 0.0 };
        bool has_data{ false };
    };

    std::vector<SymbolWork> symbol_work;
    symbol_work.reserve(per_symbol_active_ids_.size());
    for (size_t sym_id : per_symbol_active_ids_) {
        if (sym_id >= kline_by_id_.size() || kline_by_id_[sym_id] == nullptr) {
            continue;
        }
        const auto& indices = per_symbol_[sym_id];
        if (indices.empty()) {
            continue;
        }
        symbol_work.push_back(SymbolWork{ sym_id, &indices });
    }

    std::vector<SymbolPlan> symbol_plans(symbol_work.size());

    auto build_plan = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const size_t sym_id = symbol_work[i].sym_id;
            const auto& indices = *symbol_work[i].indices;

            if (sym_id >= kline_by_id_.size()) {
                continue;
            }
            const KlineDto* kptr = kline_by_id_[sym_id];
            if (!kptr) {
                continue;
            }
            const KlineDto& k = *kptr;

            const double total_vol_init = remaining_vol_[sym_id];
            if (total_vol_init <= 0.0) {
                continue;
            }

            const bool use_dir_liq = (has_dir_liq_[sym_id] != 0);
            double total_vol = total_vol_init;
            double buy_liq = remaining_liq_[sym_id].first;
            double sell_liq = remaining_liq_[sym_id].second;

            if (use_dir_liq && buy_liq <= 0.0 && sell_liq <= 0.0) {
                continue;
            }

            auto& plan = symbol_plans[i];
            plan.fills.clear();
            plan.fills.reserve(indices.size());
            plan.has_data = true;

            const size_t max_checks = max_match_orders_per_symbol_;
            size_t checked = 0;
            for (size_t idx : indices) {
                if (max_checks > 0 && checked >= max_checks) {
                    break;
                }
                if (total_vol <= 0.0) break;

                const auto [can_fill, is_taker] = policies_.can_fill_and_taker
                    ? policies_.can_fill_and_taker(open_orders_[idx], k)
                    : fill_model.can_fill_and_taker(open_orders_[idx], k);
                if (!can_fill) continue;

                const Order& ord = open_orders_[idx];
                double fill_qty = 0.0;
                if (use_dir_liq) {
                    const bool order_is_buy = (ord.side == OrderSide::Buy);
                    double& dir_liq = order_is_buy ? sell_liq : buy_liq;
                    if (dir_liq <= 0.0) continue;
                    fill_qty = std::min({ ord.quantity, total_vol, dir_liq });
                    if (fill_qty < 1e-8) continue;
                    total_vol -= fill_qty;
                    dir_liq -= fill_qty;
                }
                else {
                    fill_qty = std::min(ord.quantity, total_vol);
                    if (fill_qty < 1e-8) continue;
                    total_vol -= fill_qty;
                }

                const double fill_price = policies_.execution_price
                    ? policies_.execution_price(ord, k, market_execution_slippage_, limit_execution_slippage_)
                    : PriceExecutionModel{ market_execution_slippage_, limit_execution_slippage_ }.execution_price(ord, k);

                plan.fills.push_back(FillPlanEntry{ idx, fill_qty, fill_price, is_taker });
                ++checked;
            }

            plan.remaining_vol = total_vol;
            plan.remaining_buy_liq = buy_liq;
            plan.remaining_sell_liq = sell_liq;
        }
    };

    const size_t symbol_count = symbol_work.size();
    if (symbol_count > 1) {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const size_t task_count = std::min(symbol_count, static_cast<size_t>(hw));
        if (task_count > 1) {
            const size_t chunk = (symbol_count + task_count - 1) / task_count;
            std::vector<std::future<void>> tasks;
            tasks.reserve(task_count);
            for (size_t t = 0; t < task_count; ++t) {
                const size_t begin = t * chunk;
                if (begin >= symbol_count) break;
                const size_t end = std::min(symbol_count, begin + chunk);
                tasks.emplace_back(std::async(std::launch::async, build_plan, begin, end));
            }
            for (auto& task : tasks) {
                task.get();
            }
        }
        else {
            build_plan(0, symbol_count);
        }
    }
    else if (symbol_count == 1) {
        build_plan(0, 1);
    }

    for (size_t s = 0; s < symbol_work.size(); ++s) {
        if (!symbol_plans[s].has_data) continue;
        const size_t sym_id = symbol_work[s].sym_id;
        remaining_vol_[sym_id] = symbol_plans[s].remaining_vol;
        remaining_liq_[sym_id] = { symbol_plans[s].remaining_buy_liq, symbol_plans[s].remaining_sell_liq };

        auto& fills = symbol_plans[s].fills;
        for (const auto& entry : fills) {
            Order& ord = open_orders_[entry.idx];
            const double fill_qty = entry.fill_qty;
            const double fill_price = entry.fill_price;
            const bool is_taker = entry.is_taker;
            const double order_qty = ord.quantity;
            const double order_price = ord.price;

            const double notional = fill_qty * fill_price;
            const double feeRate = fee_model.fee_rate(is_taker);
            const double fee = notional * feeRate;

            keep_open_order[entry.idx] = false;

            // Reuse single-order leftover buffer (avoids per-fill tiny vector alloc).
            single_leftover.clear();

            if (ord.closing_position_id >= 0) {
                processClosingOrder(ord, fill_qty, fill_price, fee, single_leftover);
            }
            else {
                processOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, single_leftover);
            }

            double remaining_qty = 0.0;
            if (!single_leftover.empty()) {
                ord = single_leftover.front();
                keep_open_order[entry.idx] = true;
                remaining_qty = ord.quantity;
            }

            FillEvent fill{};
            fill.order_id = ord.id;
            fill.symbol = ord.symbol;
            fill.side = ord.side;
            fill.position_side = ord.position_side;
            fill.reduce_only = ord.reduce_only;
            fill.order_qty = order_qty;
            fill.order_price = order_price;
            fill.exec_qty = fill_qty;
            fill.exec_price = fill_price;
            fill.remaining_qty = remaining_qty;
            fill.is_taker = is_taker;
            fill.fee = fee;
            fill.fee_rate = feeRate;
            fill.closing_position_id = ord.closing_position_id;
            auto itp = symbol_kline.find(fill.symbol);
            if (itp != symbol_kline.end()) {
                const double cp = itp->second.ClosePrice;
                for (auto& pos : positions_) {
                    if (pos.symbol != fill.symbol) continue;
                    pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
                }
            }
            mark_balance_dirty_();
            fill.balance_snapshot = get_balance();
            fill.positions_snapshot = positions_;
            fill_events_.push_back(std::move(fill));
        }
    }

    // Rebuild open_orders_ preserving original time order for orders that remain.
    // If any order was filled/removed, keep_open_order will have at least one false.
    if (!keep_open_order.empty()) {
        for (bool keep : keep_open_order) {
            if (!keep) { dirty = true; open_orders_changed = true; break; }
        }
    }

    next_open_orders.clear();
    next_open_orders.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        if (keep_open_order[i]) next_open_orders.push_back(open_orders_[i]);
    }
    open_orders_.assign(next_open_orders.begin(), next_open_orders.end());
    rebuild_open_order_index_();
    if (open_orders_changed) {
        mark_open_orders_dirty_();
    }

    // Remove positions with negligible quantity.
    const size_t positions_before = positions_.size();
    positions_.erase(
        std::remove_if(positions_.begin(), positions_.end(),
            [](const Position& p) { return p.quantity <= 1e-8; }),
        positions_.end()
    );
    if (positions_.size() != positions_before) {
        mark_balance_dirty_();
    }

    merge_positions();
    rebuild_position_index_();

    // Recalculate unrealized PnL (markPrice=Close).
    for (auto& pos : positions_) {
        auto itp = symbol_kline.find(pos.symbol);
        if (itp != symbol_kline.end()) {
            double cp = itp->second.ClosePrice;
            pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
    }

    auto snapshot = get_balance();
    if (snapshot.MarginBalance < snapshot.MaintenanceMargin && !positions_.empty()) {
        constexpr int kMaxLiquidationStepsPerTick = 8;

        std::cerr << "[update_positions] Liquidation triggered! marginBalance=" << snapshot.MarginBalance
            << ", maintenanceMargin=" << snapshot.MaintenanceMargin << "\n";

        const size_t open_before = open_orders_.size();
        open_orders_.erase(
            std::remove_if(open_orders_.begin(), open_orders_.end(),
                [](const Order& o) { return o.closing_position_id < 0; }),
            open_orders_.end());
        if (open_orders_.size() != open_before) {
            open_orders_changed = true;
            mark_open_orders_dirty_();
        }
        rebuild_open_order_index_();

        for (int step = 0; step < kMaxLiquidationStepsPerTick; ++step) {
            snapshot = get_balance();
            if (positions_.empty()) break;
            if (snapshot.MarginBalance >= snapshot.MaintenanceMargin) break;

            int worst_idx = -1;
            double worst_unreal = 0.0;
            for (int i = 0; i < static_cast<int>(positions_.size()); ++i) {
                if (i == 0 || positions_[i].unrealized_pnl < worst_unreal) {
                    worst_unreal = positions_[i].unrealized_pnl;
                    worst_idx = i;
                }
            }
            if (worst_idx < 0) break;

            Position& pos = positions_[worst_idx];
            auto itK = symbol_kline.find(pos.symbol);
            if (itK == symbol_kline.end()) break;

            const KlineDto& k = itK->second;

            const double liq_price = policies_.liquidation_price
                ? policies_.liquidation_price(pos, k)
                : (pos.is_long ? k.LowPrice : k.HighPrice);
            if (liq_price <= 0.0) break;

            double volAvail = 0.0;
            const size_t sym_id = get_symbol_id_(pos.symbol);
            if (sym_id < remaining_vol_.size()) {
                volAvail = remaining_vol_[sym_id];
            }
            if (volAvail <= 1e-8) break;

            const double dir = pos.is_long ? 1.0 : -1.0;
            const double pnl_per_unit = (liq_price - pos.entry_price) * dir;
            const double fee_per_unit = (liq_price * fee_model.taker_fee);
            const double maint_per_unit = (pos.quantity > 1e-12) ? (pos.maintenance_margin / pos.quantity) : 0.0;

            const double deficit = snapshot.MaintenanceMargin - snapshot.MarginBalance;
            const double denom = (pnl_per_unit - fee_per_unit + maint_per_unit);

            double desired_close = pos.quantity;
            if (deficit > 0.0 && denom > 1e-12) {
                desired_close = std::min(pos.quantity, deficit / denom);
            }

            desired_close = std::clamp(desired_close, 1e-8, pos.quantity);

            const double close_qty = std::min({ pos.quantity, volAvail, desired_close });
            if (close_qty <= 1e-8) break;

            if (sym_id < remaining_vol_.size()) {
                remaining_vol_[sym_id] = volAvail - close_qty;
            }

            const OrderSide liqSide = pos.is_long ? OrderSide::Sell : OrderSide::Buy;
            Order liqOrd{
                -999999,
                pos.symbol,
                close_qty,
                liq_price,
                liqSide,
                PositionSide::Both,
                false,
                pos.id
            };

            const double notional = close_qty * liq_price;
            const double fee = notional * fee_model.taker_fee;
            std::vector<Order> leftover;
            leftover.reserve(1);
            processClosingOrder(liqOrd, close_qty, liq_price, fee, leftover);

            FillEvent fill{};
            fill.order_id = liqOrd.id;
            fill.symbol = liqOrd.symbol;
            fill.side = liqOrd.side;
            fill.position_side = liqOrd.position_side;
            fill.reduce_only = liqOrd.reduce_only;
            fill.order_qty = liqOrd.quantity;
            fill.order_price = liqOrd.price;
            fill.exec_qty = close_qty;
            fill.exec_price = liq_price;
            fill.remaining_qty = 0.0;
            fill.is_taker = true;
            fill.fee = fee;
            fill.fee_rate = fee_model.taker_fee;
            fill.closing_position_id = liqOrd.closing_position_id;
            auto itp = symbol_kline.find(fill.symbol);
            if (itp != symbol_kline.end()) {
                const double cp = itp->second.ClosePrice;
                for (auto& p : positions_) {
                    if (p.symbol != fill.symbol) continue;
                    p.unrealized_pnl = (cp - p.entry_price) * p.quantity * (p.is_long ? 1.0 : -1.0);
                }
            }
            mark_balance_dirty_();
            fill.balance_snapshot = get_balance();
            fill.positions_snapshot = positions_;
            fill_events_.push_back(std::move(fill));

            positions_.erase(
                std::remove_if(positions_.begin(), positions_.end(),
                    [](const Position& p) { return p.quantity <= 1e-8; }),
                positions_.end());

            merge_positions();
            rebuild_position_index_();

            for (auto& p : positions_) {
                auto itp = symbol_kline.find(p.symbol);
                if (itp != symbol_kline.end()) {
                    double cp = itp->second.ClosePrice;
                    p.unrealized_pnl = (cp - p.entry_price) * p.quantity * (p.is_long ? 1.0 : -1.0);
                }
            }
        }

        snapshot = get_balance();
        if (!positions_.empty() && snapshot.MarginBalance < snapshot.MaintenanceMargin) {
            std::cerr << "[update_positions] Liquidation unresolved after steps, forcing account bankruptcy\n";
            wallet_balance_ = 0.0;
            used_margin_ = 0.0;
            positions_.clear();
            mark_balance_dirty_();
            position_indices_by_symbol_.clear();
            if (!open_orders_.empty()) {
                open_orders_.clear();
                open_orders_changed = true;
                mark_open_orders_dirty_();
            }
            order_to_position_.clear();
            open_order_index_by_id_.clear();
            position_index_by_id_.clear();
        }
    }

    for (auto& p : positions_) {
        auto itp = symbol_kline.find(p.symbol);
        if (itp == symbol_kline.end()) continue;

        const double mark = itp->second.ClosePrice;
        p.notional = std::abs(p.quantity * mark);
        double mmr, maxLev;
        std::tie(mmr, maxLev) = get_tier_info(p.notional);
        p.maintenance_margin = p.notional * mmr;
    }

    snapshot = get_balance();
    if (enable_console_output_) {
        std::cout << "[update_positions] marginBalance=" << snapshot.MarginBalance
            << ", maintenanceMargin=" << snapshot.MaintenanceMargin
            << ", walletBalance=" << snapshot.WalletBalance;
        for (const auto& kv : symbol_kline) {
            std::cout << ", " << kv.first << "=" << kv.second.ClosePrice;
        }
        std::cout << std::endl;
    }

    // Track changes from position cleanup/merge as well.
    // If any position was removed by epsilon-filter, quantity set changed.
    // (We can't easily know removed count without scanning; mark dirty if we had any positions going into cleanup.)
    // Any liquidation path or position size adjustment affects externally visible state.
    if (!open_orders_.empty()) {
        dirty = true;
    }

    if (dirty) {
        ++state_version_;
    }
}

std::vector<Account::FundingApplyResult> Account::apply_funding(
    const std::string& symbol, uint64_t /*funding_time*/, double rate, double mark_price)
{
    std::vector<FundingApplyResult> results;
    if (!std::isfinite(mark_price) || mark_price <= 0.0) {
        return results;
    }

    double total_delta = 0.0;

    for (auto& pos : positions_) {
        if (pos.symbol != symbol) continue;
        if (pos.quantity <= 0.0) continue;

        const double notional = std::abs(pos.quantity) * mark_price;
        if (notional <= 0.0) continue;

        const double dir = pos.is_long ? -1.0 : 1.0;
        const double funding = notional * rate * dir;
        total_delta += funding;
        results.push_back(FundingApplyResult{ pos.id, pos.is_long, pos.quantity, funding });
    }

    if (results.empty()) {
        return results;
    }

    wallet_balance_ += total_delta;
    balance_ += total_delta;
    mark_balance_dirty_();
    ++state_version_;
    return results;
}

std::vector<Account::FillEvent> Account::drain_fill_events()
{
    std::vector<FillEvent> out;
    out.swap(fill_events_);
    return out;
}

/// @brief Process a closing order fill: update position, free margin, realize PnL.
void Account::processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    auto itIdx = position_index_by_id_.find(ord.closing_position_id);
    if (itIdx == position_index_by_id_.end()) {
        if (enable_console_output_) {
            std::cerr << "[processClosingOrder] closing_position_id=" << ord.closing_position_id << " not found\n";
        }
        leftover.push_back(ord);
        return;
    }

    Position& pos = positions_[itIdx->second];

    double close_qty = std::min(fill_qty, pos.quantity);
    double realized_pnl = (fill_price - pos.entry_price) * close_qty * (pos.is_long ? 1.0 : -1.0);

    double ratio = close_qty / pos.quantity;
    double freed_margin = pos.initial_margin * ratio;
    double freed_maint = pos.maintenance_margin * ratio;
    double freed_fee = pos.fee * ratio;

    wallet_balance_ += realized_pnl;
    wallet_balance_ -= fee;

    used_margin_ -= freed_margin;

    pos.quantity -= close_qty;
    pos.initial_margin -= freed_margin;
    pos.maintenance_margin -= freed_maint;
    pos.fee -= freed_fee;
    pos.notional = pos.entry_price * pos.quantity;

    ord.quantity -= close_qty;
    if (ord.quantity > 1e-8)
        leftover.push_back(ord);
    mark_balance_dirty_();
}

void Account::cancel_order_by_id(int order_id) {
    auto it = open_order_index_by_id_.find(order_id);
    if (it == open_order_index_by_id_.end()) {
        if (enable_console_output_) {
            std::cerr << "[cancel_order_by_id] No open order with ID=" << order_id << "\n";
        }
        return;
    }

    // Preserve original order: erase the one element and rebuild indices.
    open_orders_.erase(open_orders_.begin() + static_cast<std::ptrdiff_t>(it->second));
    rebuild_open_order_index_();
    mark_open_orders_dirty_();
    ++state_version_;
}

void Account::cancel_open_orders(const std::string& symbol) {
    if (open_orders_.empty()) {
        return;
    }

    const size_t before = open_orders_.size();
    open_orders_.erase(
        std::remove_if(open_orders_.begin(), open_orders_.end(),
            [&](const Order& o) { return o.symbol == symbol; }),
        open_orders_.end());

    if (open_orders_.size() == before) {
        return;
    }

    rebuild_open_order_index_();
    mark_open_orders_dirty_();
    ++state_version_;
}

void Account::close_position(const std::string& symbol, double price) {
    bool found = false;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it != position_indices_by_symbol_.end()) {
        for (size_t idx : it->second) {
            const auto& pos = positions_[idx];
            if (pos.symbol != symbol) continue;
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    rebuild_open_order_index_();
    if (found) {
        ++state_version_;
    }

    if (!found) {
        if (enable_console_output_) {
            std::cerr << "[close_position] No position found for symbol=" << symbol << "\n";
        }
    }
}

void Account::close_position(const std::string& symbol) {
    close_position(symbol, 0.0);
}

/// @brief Close only one side in hedge mode.
void Account::close_position(const std::string& symbol, QTrading::Dto::Trading::PositionSide position_side, double price) {
    if (!hedge_mode_) {
        if (position_side != PositionSide::Both) {
            if (enable_console_output_) {
                std::cerr << "[close_position] One-way mode requires position_side=Both\n";
            }
            return;
        }
        close_position(symbol, price);
        return;
    }

    if (position_side == PositionSide::Both) {
        close_position(symbol, price);
        return;
    }

    const bool want_long = (position_side == PositionSide::Long);
    bool found = false;
    auto it = position_indices_by_symbol_.find(symbol);
    if (it != position_indices_by_symbol_.end()) {
        for (size_t idx : it->second) {
            const auto& pos = positions_[idx];
            if (pos.symbol == symbol && pos.is_long == want_long) {
                found = true;
                place_closing_order(pos.id, pos.quantity, price);
            }
        }
    }
    rebuild_open_order_index_();
    if (found) {
        ++state_version_;
    }

    if (!found) {
        if (enable_console_output_) {
            std::cerr << "[close_position] No " << (want_long ? "LONG" : "SHORT")
                << " position found for symbol=" << symbol << "\n";
        }
    }
}

/// @brief Get a snapshot of all open orders.
/// @return Const reference to open_orders_.
const std::vector<Order>& Account::get_all_open_orders() const {
    return open_orders_;
}

/// @brief Get a snapshot of all positions.
/// @return Const reference to positions_.
const std::vector<Position>& Account::get_all_positions() const {
    return positions_;
}

/// @brief Find the maintenance margin rate and max leverage for a given notional.
/// @param notional Position notional.
/// @return Tuple(maintenance_margin_rate, max_leverage).
std::tuple<double, double> Account::get_tier_info(double notional) const {
    for (const auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    return std::make_tuple(margin_tiers.front().maintenance_margin_rate, margin_tiers.front().max_leverage);
}

/// @brief Get maker and taker fee rates based on VIP level.
/// @return Tuple(maker_fee_rate, taker_fee_rate).
std::tuple<double, double> Account::get_fee_rates() const {
    if (policies_.fee_rates) {
        return policies_.fee_rates(vip_level_);
    }
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
}

/// @brief Adjust leverage on existing positions for a symbol.
bool Account::adjust_position_leverage(const std::string& symbol, double oldLev, double newLev) {
    std::vector<std::reference_wrapper<Position>> related;
    for (auto& pos : positions_) {
        if (pos.symbol == symbol) {
            related.push_back(pos);
        }
    }
    if (related.empty())
        return true;

    double totalDiff = 0.0;
    std::vector<double> newMaint(related.size());
    for (size_t i = 0; i < related.size(); ++i) {
        Position& p = related[i].get();
        double mmr, maxLev;
        std::tie(mmr, maxLev) = get_tier_info(p.notional);
        if (newLev > maxLev) {
            if (enable_console_output_) {
                std::cerr << "[adjust_position_leverage] newLev=" << newLev << " > maxLev=" << maxLev << "\n";
            }
            return false;
        }
        double oldM = p.initial_margin;
        double newM = p.notional / newLev;
        double diff = newM - oldM;
        totalDiff += diff;
        newMaint[i] = p.notional * mmr;
    }
    double eq = get_equity();
    if (totalDiff > 0) {
        if (eq < totalDiff) {
            if (enable_console_output_) {
                std::cerr << "[adjust_position_leverage] Not enough equity.\n";
            }
            return false;
        }
        balance_ -= totalDiff;
        used_margin_ += totalDiff;
    }
    else {
        balance_ += std::fabs(totalDiff);
        used_margin_ -= std::fabs(totalDiff);
    }
    for (size_t i = 0; i < related.size(); i++) {
        Position& p = related[i].get();
        p.initial_margin = p.notional / newLev;
        p.leverage = newLev;
        p.maintenance_margin = newMaint[i];
    }
    mark_balance_dirty_();
    return true;
}

/// @brief Process a reduce_only opening order fill.
bool Account::processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    if (!has_reducible_position(positions_, ord, hedge_mode_)) {
        return false;
    }

    for (auto& pos : positions_) {
        if (!order_closes_position(ord, pos, hedge_mode_)) continue;

        Order closeOrd{
            ord.id,
            ord.symbol,
            ord.quantity,
            ord.price,
            ord.side,
            hedge_mode_ ? ord.position_side : PositionSide::Both,
            ord.reduce_only,
            pos.id
        };

        std::vector<Order> tmp;
        tmp.reserve(1);
        processClosingOrder(closeOrd, fill_qty, fill_price, fee, tmp);

        ord.quantity = closeOrd.quantity;
        if (ord.quantity > 1e-8) leftover.push_back(ord);
        return true;
    }

    return false;
}

void Account::processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover) {

    double lev = get_symbol_leverage(ord.symbol);
    double mmr, maxLev;
    std::tie(mmr, maxLev) = get_tier_info(notional);
    if (lev > maxLev) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Leverage too high for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return;
    }

    const double init_margin = notional / lev;
    const double maint_margin = notional * mmr;

    const auto snap = get_balance();
    const double required = init_margin + fee;
    if (snap.AvailableBalance < required) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Not enough available balance for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return;
    }

    if (hedge_mode_ && ord.position_side == PositionSide::Both) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Hedge-mode order must specify position_side (Long/Short). id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return;
    }

    wallet_balance_ -= fee;
    used_margin_ += init_margin;

    auto pm = order_to_position_.find(ord.id);
    if (pm == order_to_position_.end()) {
        int pid = generate_position_id();
        const bool pos_is_long = hedge_mode_
            ? (ord.position_side == PositionSide::Long)
            : (ord.side == OrderSide::Buy);
        positions_.push_back(Position{
            pid,
            ord.id,
            ord.symbol,
            fill_qty,
            fill_price,
            pos_is_long,
            0.0,
            notional,
            init_margin,
            maint_margin,
            fee,
            lev,
            feeRate
            });
        order_to_position_[ord.id] = pid;
        rebuild_position_index_();
    }
    else {
        int pid = pm->second;
        auto itPosIdx = position_index_by_id_.find(pid);
        if (itPosIdx != position_index_by_id_.end()) {
            Position& pos = positions_[itPosIdx->second];
            double old_notional = pos.notional;
            double new_notional = old_notional + notional;
            double old_qty = pos.quantity;
            double new_qty = old_qty + fill_qty;
            double new_entry = new_notional / new_qty;

            pos.quantity = new_qty;
            pos.entry_price = new_entry;
            pos.notional += notional;
            pos.initial_margin += init_margin;
            pos.maintenance_margin += maint_margin;
            pos.fee += fee;
        }
        else {
            // Fallback: index out of sync, do linear search.
            for (auto& pos : positions_) {
                if (pos.id == pid) {
                    double old_notional = pos.notional;
                    double new_notional = old_notional + notional;
                    double old_qty = pos.quantity;
                    double new_qty = old_qty + fill_qty;
                    double new_entry = new_notional / new_qty;

                    pos.quantity = new_qty;
                    pos.entry_price = new_entry;
                    pos.notional += notional;
                    pos.initial_margin += init_margin;
                    pos.maintenance_margin += maint_margin;
                    pos.fee += fee;
                    break;
                }
            }
        }
    }

    mark_balance_dirty_();

    double leftoverQty = ord.quantity - fill_qty;
    if (leftoverQty > 1e-8) {
        ord.quantity = leftoverQty;
        leftover.push_back(ord);
    }
}

Account::Policies Account::DefaultPolicies()
{
    return AccountPolicies::Default();
}
