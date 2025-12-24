#include "Exchanges/BinanceSimulator/Futures/Config.hpp"
#include "Exchanges/BinanceSimulator/Futures/Account.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <unordered_map>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace {

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
    next_position_id_(1)
{
    open_orders_.reserve(1024);
    positions_.reserve(1024);
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
    return build_snapshot(wallet_balance_, positions_, open_orders_, symbol_leverage_, last_mark_price_, market_slippage_buffer_);
}

void Account::set_market_slippage_buffer(double pct)
{
    market_slippage_buffer_ = pct;
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
        std::cerr << "[set_position_mode] Cannot switch mode while positions are open.\n";
        return;
    }
    hedge_mode_ = hedgeMode;
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
    }
    else {
        if (adjust_position_leverage(symbol, oldLev, newLeverage)) {
            it->second = newLeverage;
        }
        else {
            std::cerr << "[set_symbol_leverage] Not enough equity to adjust.\n";
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
        std::cerr << "[place_order] Invalid quantity <= 0\n";
        return false;
    }

    if (!hedge_mode_ && position_side != PositionSide::Both) {
        position_side = PositionSide::Both;
    }

    // Binance-like: in hedge mode the caller must specify Long/Short (no inference).
    if (hedge_mode_ && position_side == PositionSide::Both) {
        std::cerr << "[place_order] Hedge-mode order must specify position_side (Long/Short).\n";
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
            std::cerr << "[place_order] reduce_only rejected: no reducible position.\n";
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
    for (auto& pos : positions_) {
        if (pos.symbol != symbol) continue;

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
        return true;
    }
    return false;
}

/// @brief Create a closing order for a specific position.
/// @param position_id  ID of the position to close.
/// @param quantity     Amount to close.
/// @param price        Close price (<=0 = market).
void Account::place_closing_order(int position_id, double quantity, double price) {
    for (const auto& pos : positions_) {
        if (pos.id == position_id) {
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
            return;
        }
    }
    std::cerr << "[place_closing_order] position_id=" << position_id << " not found\n";
}


/// @brief Merge positions of the same symbol & direction into one.
/// @details Aggregates quantities and recalculates weighted entry price, margin, fees.
void Account::merge_positions() {
    if (positions_.empty()) return;

    // Define a key by symbol and direction.
    struct Key {
        std::string sym;
        bool dir;
        bool operator==(const Key& o) const {
            return (sym == o.sym && dir == o.dir);
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            auto h1 = std::hash<std::string>()(k.sym);
            auto h2 = std::hash<bool>()(k.dir);
            return (h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2)));
        }
    };

    std::unordered_map<Key, Position, KeyHash> merged;
    for (auto& pos : positions_) {
        Key key{ pos.symbol, pos.is_long };
        if (merged.find(key) == merged.end()) {
            merged[key] = pos;
        }
        else {
            Position& p0 = merged[key];
            double totalQty = p0.quantity + pos.quantity;
            if (totalQty < 1e-8) {
                p0.quantity = 0;
                continue;
            }
            double weightedPrice = (p0.entry_price * p0.quantity + pos.entry_price * pos.quantity) / totalQty;
            p0.quantity = totalQty;
            p0.entry_price = weightedPrice;
            p0.notional += pos.notional;
            p0.initial_margin += pos.initial_margin;
            p0.maintenance_margin += pos.maintenance_margin;
            p0.fee += pos.fee;
        }
    }

    positions_.clear();
    positions_.reserve(merged.size());
    for (auto& kv : merged) {
        if (kv.second.quantity > 1e-8)
            positions_.push_back(kv.second);
    }
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
    // Cache mark prices for margin estimation.
    for (const auto& kv : symbol_kline) {
        last_mark_price_[kv.first] = kv.second.ClosePrice;
    }

    double makerFee, takerFee;
    std::tie(makerFee, takerFee) = get_fee_rates();

    std::unordered_map<std::string, double> remaining_vol;
    remaining_vol.reserve(symbol_kline.size());
    for (const auto& kv : symbol_kline) {
        remaining_vol.emplace(kv.first, kv.second.Volume);
    }

    auto can_fill_and_taker = [](const Order& ord, const KlineDto& k) -> std::pair<bool, bool> {
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
    };

    std::unordered_map<std::string, std::vector<size_t>> per_symbol;
    per_symbol.reserve(symbol_kline.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        per_symbol[open_orders_[i].symbol].push_back(i);
    }

    std::vector<bool> keep(open_orders_.size(), true);

    for (auto& symKv : per_symbol) {
        const std::string& sym = symKv.first;
        auto itK = symbol_kline.find(sym);
        if (itK == symbol_kline.end()) {
            continue;
        }
        const KlineDto& k = itK->second;

        auto itVol = remaining_vol.find(sym);
        if (itVol == remaining_vol.end() || itVol->second <= 0.0) {
            continue;
        }

        auto& indices = symKv.second;

        std::vector<size_t> fillable;
        fillable.reserve(indices.size());
        for (size_t idx : indices) {
            const auto [can_fill, _] = can_fill_and_taker(open_orders_[idx], k);
            if (can_fill) fillable.push_back(idx);
        }

        std::sort(fillable.begin(), fillable.end(), [&](size_t a, size_t b) {
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
        });

        for (size_t idx : fillable) {
            if (itVol->second <= 0.0) break;

            Order& ord = open_orders_[idx];
            const auto [can_fill, is_taker] = can_fill_and_taker(ord, k);
            if (!can_fill) continue;

            const double fill_qty = std::min(ord.quantity, itVol->second);
            if (fill_qty < 1e-8) continue;

            itVol->second -= fill_qty;

            const bool is_market = (ord.price <= 0.0);
            const double fill_price = is_market ? k.ClosePrice : ord.price;

            const double notional = fill_qty * fill_price;
            const double feeRate = (is_taker ? takerFee : makerFee);
            const double fee = notional * feeRate;

            keep[idx] = false;

            std::vector<Order> single_leftover;
            single_leftover.reserve(1);

            if (ord.closing_position_id >= 0) {
                processClosingOrder(ord, fill_qty, fill_price, fee, single_leftover);
            }
            else {
                processOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, single_leftover);
            }

            // If partially filled, keep the remainder as open.
            if (!single_leftover.empty()) {
                ord = single_leftover.front();
                keep[idx] = true;
            }
        }
    }

    // Rebuild open_orders_ preserving original time order for orders that remain.
    std::vector<Order> next_open;
    next_open.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        if (keep[i]) next_open.push_back(open_orders_[i]);
    }
    open_orders_.swap(next_open);

    // Remove positions with negligible quantity.
    positions_.erase(
        std::remove_if(positions_.begin(), positions_.end(),
            [](const Position& p) { return p.quantity <= 1e-8; }),
        positions_.end()
    );

    merge_positions();

    // Recalculate unrealized PnL (markPrice=Close).
    for (auto& pos : positions_) {
        auto itp = symbol_kline.find(pos.symbol);
        if (itp != symbol_kline.end()) {
            double cp = itp->second.ClosePrice;
            pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
        }
    }

    // Progressive liquidation (Binance-like approximation)
    // - Trigger if marginBalance < maintenanceMargin
    // - Pick the most losing position first (most negative unrealized_pnl)
    // - Close using worst-case OHLC: long -> Low, short -> High
    // - Iterate a bounded number of steps per tick

    auto snapshot = get_balance();
    if (snapshot.MarginBalance < snapshot.MaintenanceMargin && !positions_.empty()) {
        constexpr int kMaxLiquidationStepsPerTick = 8;

        std::cerr << "[update_positions] Liquidation triggered! marginBalance=" << snapshot.MarginBalance
            << ", maintenanceMargin=" << snapshot.MaintenanceMargin << "\n";

        // Cancel all opening orders during liquidation (risk-off). Keep closing orders.
        open_orders_.erase(
            std::remove_if(open_orders_.begin(), open_orders_.end(),
                [](const Order& o) { return o.closing_position_id < 0; }),
            open_orders_.end());

        for (int step = 0; step < kMaxLiquidationStepsPerTick; ++step) {
            // Refresh snapshot
            snapshot = get_balance();
            if (positions_.empty()) break;
            if (snapshot.MarginBalance >= snapshot.MaintenanceMargin) break;
            // Do not stop liquidation based on wallet being <= 0; wallet may go negative in extreme cases.

            // Find most losing position (minimum unrealized PnL)
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

            // Worst-case liquidation fill price.
            const double liq_price = pos.is_long ? k.LowPrice : k.HighPrice;
            if (liq_price <= 0.0) break;

            // Use available kline volume to bound liquidation amount.
            double volAvail = 0.0;
            auto itVol = remaining_vol.find(pos.symbol);
            if (itVol != remaining_vol.end()) volAvail = itVol->second;
            if (volAvail <= 1e-8) break;

            // Close just enough to restore marginBalance >= maintenanceMargin, if possible.
            // Approximate required close quantity by assuming:
            //  - realized PnL per unit = (liq_price - entry_price) * dir
            //  - maintenance margin reduces proportionally with position size
            //  - fees reduce wallet balance: takerFee * liq_price per unit
            const double dir = pos.is_long ? 1.0 : -1.0;
            const double pnl_per_unit = (liq_price - pos.entry_price) * dir;
            const double fee_per_unit = (liq_price * takerFee);
            const double maint_per_unit = (pos.quantity > 1e-12) ? (pos.maintenance_margin / pos.quantity) : 0.0;

            // Current deficit: need (MarginBalance - MaintenanceMargin) >= 0
            const double deficit = snapshot.MaintenanceMargin - snapshot.MarginBalance;

            // Closing q changes the inequality by approximately:
            //   new(MB-MM) ≈ (MB-MM) + q * (pnl_per_unit - fee_per_unit) + q * maint_per_unit
            // We want new(MB-MM) >= 0 => q >= deficit / (pnl_per_unit - fee_per_unit + maint_per_unit)
            const double denom = (pnl_per_unit - fee_per_unit + maint_per_unit);

            double desired_close = pos.quantity;
            if (deficit > 0.0 && denom > 1e-12) {
                desired_close = std::min(pos.quantity, deficit / denom);
            }

            // Guard: if denom <= 0, closing doesn't improve the condition in this approximation.
            // In that case, fall back to closing as much as possible.
            desired_close = std::clamp(desired_close, 1e-8, pos.quantity);

            const double close_qty = std::min({ pos.quantity, volAvail, desired_close });
            if (close_qty <= 1e-8) break;

            remaining_vol[pos.symbol] = volAvail - close_qty;

            // Build a synthetic closing order against this position.
            const OrderSide liqSide = pos.is_long ? OrderSide::Sell : OrderSide::Buy;
            Order liqOrd{
                -999999, // synthetic
                pos.symbol,
                close_qty,
                liq_price,
                liqSide,
                PositionSide::Both,
                false,
                pos.id
            };

            const double notional = close_qty * liq_price;
            const double fee = notional * takerFee;
            std::vector<Order> leftover;
            leftover.reserve(1);
            processClosingOrder(liqOrd, close_qty, liq_price, fee, leftover);

            // Cleanup empty positions and refresh PnL for remaining.
            positions_.erase(
                std::remove_if(positions_.begin(), positions_.end(),
                    [](const Position& p) { return p.quantity <= 1e-8; }),
                positions_.end());

            merge_positions();

            for (auto& p : positions_) {
                auto itp = symbol_kline.find(p.symbol);
                if (itp != symbol_kline.end()) {
                    double cp = itp->second.ClosePrice;
                    p.unrealized_pnl = (cp - p.entry_price) * p.quantity * (p.is_long ? 1.0 : -1.0);
                }
            }
        }

        // If still in liquidation after steps, and nothing left to do, force clear.
        snapshot = get_balance();
        if (!positions_.empty() && snapshot.MarginBalance < snapshot.MaintenanceMargin) {
            std::cerr << "[update_positions] Liquidation unresolved after steps, forcing account bankruptcy\n";
            wallet_balance_ = 0.0;
            used_margin_ = 0.0;
            positions_.clear();
            open_orders_.clear();
            order_to_position_.clear();
        }
    }

    // Recompute notional/maintenance margin tier-aware (markPrice=Close) after any changes.
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
    std::cout << "[update_positions] marginBalance=" << snapshot.MarginBalance
        << ", maintenanceMargin=" << snapshot.MaintenanceMargin
        << ", walletBalance=" << snapshot.WalletBalance;
    for (const auto& kv : symbol_kline) {
        std::cout << ", " << kv.first << "=" << kv.second.ClosePrice;
    }
    std::cout << std::endl;
}

/// @brief Process a closing order fill: update position, free margin, realize PnL.
void Account::processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    bool foundPos = false;
    for (auto& pos : positions_) {
        if (pos.id == ord.closing_position_id) {
            foundPos = true;
            double close_qty = std::min(fill_qty, pos.quantity);
            double realized_pnl = (fill_price - pos.entry_price) * close_qty * (pos.is_long ? 1.0 : -1.0);

            double ratio = close_qty / pos.quantity;
            double freed_margin = pos.initial_margin * ratio;
            double freed_maint = pos.maintenance_margin * ratio;
            double freed_fee = pos.fee * ratio;

            // Cross margin: realized PnL affects wallet; margin is merely released (tracking only).
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
            break;
        }
    }
    if (!foundPos) {
        std::cerr << "[processClosingOrder] closing_position_id=" << ord.closing_position_id << " not found\n";
        leftover.push_back(ord);
    }
}

/// @brief Process a reduce_only opening order fill.
bool Account::processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover) {
    // Use the same eligibility predicate as placement-time to keep semantics consistent.
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
        std::cerr << "[processNormalOpeningOrder] Leverage too high for order id=" << ord.id << "\n";
        leftover.push_back(ord);
        return;
    }

    const double init_margin = notional / lev;
    const double maint_margin = notional * mmr;

    const auto snap = get_balance();
    const double required = init_margin + fee;
    if (snap.AvailableBalance < required) {
        std::cerr << "[processNormalOpeningOrder] Not enough available balance for order id=" << ord.id << "\n";
        leftover.push_back(ord);
        return;
    }

    // Hedge-mode requires explicit position side to disambiguate intent.
    if (hedge_mode_ && ord.position_side == PositionSide::Both) {
        std::cerr << "[processNormalOpeningOrder] Hedge-mode order must specify position_side (Long/Short). id=" << ord.id << "\n";
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
    }
    else {
        int pid = pm->second;
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

    double leftoverQty = ord.quantity - fill_qty;
    if (leftoverQty > 1e-8) {
        ord.quantity = leftoverQty;
        leftover.push_back(ord);
    }
}

/// @brief Dispatch opening order processing.
/// @param ord, fill_qty, fill_price, notional, fee, feeRate, leftover
/// @details Calls reduce_only or normal processing accordingly.
void Account::processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover) {
    if (ord.reduce_only) {
        if (!processReduceOnlyOrder(ord, fill_qty, fill_price, fee, leftover)) {
            // If no matching position to reduce, ignore the order.
        }
    }
    else {
        processNormalOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
    }
}

/// @brief Close all positions for a symbol (one-way) or both sides (hedge).
/// @param symbol Symbol to close.
void Account::close_position(const std::string& symbol, double price) {
    // In one‑way mode, close all positions for the symbol.
    // In hedge mode, this version can be customized to close both long and short positions.
    bool found = false;
    for (const auto& pos : positions_) {
        if (pos.symbol == symbol) {
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    if (!found) {
        std::cerr << "[close_position] No position found for symbol=" << symbol << "\n";
    }
}

void Account::close_position(const std::string& symbol) {
    close_position(symbol, 0.0);
}

/// @brief Close only one side in hedge mode.
void Account::close_position(const std::string& symbol, QTrading::Dto::Trading::PositionSide position_side, double price) {
    if (!hedge_mode_) {
        // One-way: only Both is valid.
        if (position_side != PositionSide::Both) {
            std::cerr << "[close_position] One-way mode requires position_side=Both\n";
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
    for (const auto& pos : positions_) {
        if (pos.symbol == symbol && pos.is_long == want_long) {
            found = true;
            place_closing_order(pos.id, pos.quantity, price);
        }
    }
    if (!found) {
        std::cerr << "[close_position] No " << (want_long ? "LONG" : "SHORT")
            << " position found for symbol=" << symbol << "\n";
    }
}

/// @brief Cancel any open order by its unique ID.
void Account::cancel_order_by_id(int order_id) {
    bool found = false;
    for (auto it = open_orders_.begin(); it != open_orders_.end();) {
        if (it->id == order_id) {
            found = true;
            it = open_orders_.erase(it);
        }
        else {
            ++it;
        }
    }
    if (!found) {
        std::cerr << "[cancel_order_by_id] No open order with ID=" << order_id << "\n";
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
            std::cerr << "[adjust_position_leverage] newLev=" << newLev << " > maxLev=" << maxLev << "\n";
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
            std::cerr << "[adjust_position_leverage] Not enough equity.\n";
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
    return true;
}
