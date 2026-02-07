#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <unordered_map>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::InstrumentType;
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

static bool is_perp_instrument(InstrumentType type) noexcept
{
    return type == InstrumentType::Perp;
}

static bool is_spot_instrument(InstrumentType type) noexcept
{
    return type == InstrumentType::Spot;
}

static Account::AccountInitConfig validate_account_init_config(const Account::AccountInitConfig& cfg)
{
    auto is_valid_amount = [](double v) {
        return std::isfinite(v) && v >= 0.0;
    };

    if (!is_valid_amount(cfg.spot_initial_cash)) {
        throw std::runtime_error("AccountInitConfig.spot_initial_cash must be finite and >= 0.");
    }
    if (!is_valid_amount(cfg.perp_initial_wallet)) {
        throw std::runtime_error("AccountInitConfig.perp_initial_wallet must be finite and >= 0.");
    }
    if (cfg.vip_level < 0) {
        throw std::runtime_error("AccountInitConfig.vip_level must be >= 0.");
    }
    return cfg;
}

} // namespace

/// @brief Simulated Binance Futures Account implementation.
/// @details Supports one-way and hedge modes, order matching, margin, fees, and auto-liquidation.
Account::Account(double initial_balance, int vip_level)
    : Account(AccountInitConfig{ 0.0, initial_balance, vip_level })
{
}

Account::Account(const AccountInitConfig& init_config)
    : spot(*this),
    perp(*this),
    spot_ledger_(0.0),
    perp_ledger_(0.0),
    vip_level_(0),
    hedge_mode_(false),
    next_order_id_(1),
    next_position_id_(1),
    policies_(DefaultPolicies()),
    tick_memory_(std::pmr::new_delete_resource())
{
    const auto cfg = validate_account_init_config(init_config);
    vip_level_ = cfg.vip_level;
    spot_ledger_.set_cash_balance(cfg.spot_initial_cash);
    perp_ledger_.set_wallet_balance(cfg.perp_initial_wallet);
    perp_ledger_.set_used_margin(0.0);

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
    last_mark_price_by_id_.reserve(1024);
    merge_indices_.reserve(1024);
    merged_positions_.reserve(1024);
}

Account::Account(double initial_balance, int vip_level, Policies policies)
    : Account(AccountInitConfig{ 0.0, initial_balance, vip_level }, std::move(policies))
{
}

Account::Account(const AccountInitConfig& init_config, Policies policies)
    : Account(init_config)
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

double Account::get_total_cash_balance() const {
    return perp_ledger_.wallet_balance() + spot_ledger_.cash_balance();
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

const QTrading::Dto::Trading::InstrumentSpec& Account::resolve_instrument_spec_(const std::string& symbol) const
{
    return instrument_registry_.Resolve(symbol);
}

void Account::set_instrument_type(const std::string& symbol, InstrumentType type)
{
    instrument_registry_.Set(symbol, type);
    if (type == InstrumentType::Spot) {
        symbol_leverage_.erase(symbol);
    }
    mark_balance_dirty_();
}

void Account::set_instrument_spec(const std::string& symbol, const QTrading::Dto::Trading::InstrumentSpec& spec)
{
    instrument_registry_.Set(symbol, spec);
    if (spec.type == InstrumentType::Spot || spec.max_leverage <= 1.0) {
        symbol_leverage_.erase(symbol);
    }
    mark_balance_dirty_();
}

QTrading::Dto::Trading::InstrumentSpec Account::get_instrument_spec(const std::string& symbol) const
{
    return resolve_instrument_spec_(symbol);
}

/// @brief Get the current leverage for a symbol.
/// @param symbol Trading symbol.
/// @return Leverage multiplier (default 1.0).
double Account::get_symbol_leverage(const std::string& symbol) const {
    const auto& spec = resolve_instrument_spec_(symbol);
    if (spec.type == InstrumentType::Spot || spec.max_leverage <= 1.0) {
        return 1.0;
    }
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
    const auto& spec = resolve_instrument_spec_(symbol);
    if (spec.type == InstrumentType::Spot || spec.max_leverage <= 1.0) {
        // Spot (or explicitly unlevered instruments) are always 1x.
        const auto erased = symbol_leverage_.erase(symbol);
        if (erased > 0) {
            mark_balance_dirty_();
        }
        return;
    }
    if (newLeverage > spec.max_leverage) {
        if (enable_console_output_) {
            std::cerr << "[set_symbol_leverage] leverage exceeds instrument max.\n";
        }
        return;
    }
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

    const auto& instrument_spec = resolve_instrument_spec_(symbol);
    if (instrument_spec.type == InstrumentType::Spot) {
        return place_spot_order(symbol, quantity, price, side, position_side, reduce_only, instrument_spec);
    }

    return place_perp_order(symbol, quantity, price, side, position_side, reduce_only, instrument_spec);
}

bool Account::has_reducible_position_for_order_(const Order& ord) const
{
    return has_reducible_position(positions_, ord, hedge_mode_);
}

bool Account::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only)
{
    return place_order(symbol, quantity, 0.0, side, position_side, reduce_only);
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
    bool positions_changed = false;
    // Reset per-tick scratch allocator before building fill buffers.
    tick_memory_.release();
    fill_events_.clear();
    if (fill_events_.capacity() < open_orders_.size()) {
        fill_events_.reserve(open_orders_.size());
    }

    bool mark_dirty = false;

    const FeeModel fee_model(get_fee_rates());
    const FillModel fill_model{ kline_volume_split_mode_ };

    if (!kline_by_id_.empty()) {
        std::fill(kline_by_id_.begin(), kline_by_id_.end(), nullptr);
    }

    const bool has_open_orders = !open_orders_.empty();
    for (const auto& kv : symbol_kline) {
        const size_t sym_id = get_symbol_id_(kv.first);
        const auto& k = kv.second;
        kline_by_id_[sym_id] = &k;
        remaining_vol_[sym_id] = k.Volume;
        last_mark_price_by_id_[sym_id] = k.ClosePrice;
        mark_dirty = true;
        if (!has_open_orders) {
            has_dir_liq_[sym_id] = 0;
            continue;
        }
        const auto [has, liq] = policies_.directional_liquidity
            ? policies_.directional_liquidity(kline_volume_split_mode_, k)
            : fill_model.build_directional_liquidity(k);
        has_dir_liq_[sym_id] = has ? 1 : 0;
        remaining_liq_[sym_id] = liq;
    }
    if (mark_dirty) {
        mark_balance_dirty_();
    }
    if (has_open_orders && per_symbol_cache_version_ != open_orders_version_) {
        rebuild_per_symbol_cache_();
        rebuild_open_order_index_();
    }
    else if (!has_open_orders) {
        if (!per_symbol_active_ids_.empty()) {
            per_symbol_active_ids_.clear();
        }
        if (!open_order_index_by_id_.empty()) {
            open_order_index_by_id_.clear();
        }
    }

    if (has_open_orders) {
        process_open_orders_pipeline_(fee_model.maker_fee, fee_model.taker_fee, dirty, open_orders_changed, positions_changed);
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
        positions_changed = true;
    }

    if (positions_changed) {
        merge_positions();
        rebuild_position_index_();
    }

    // Recalculate unrealized PnL (markPrice=Close).
    for (auto& pos : positions_) {
        const size_t pid = get_symbol_id_(pos.symbol);
        if (pid >= kline_by_id_.size()) {
            continue;
        }
        const KlineDto* pk = kline_by_id_[pid];
        if (!pk) {
            continue;
        }
        const double cp = pk->ClosePrice;
        pos.unrealized_pnl = (cp - pos.entry_price) * pos.quantity * (pos.is_long ? 1.0 : -1.0);
    }

    apply_perp_liquidation_(fee_model.taker_fee, open_orders_changed, positions_changed);

    for (auto& p : positions_) {
        auto itp = symbol_kline.find(p.symbol);
        if (itp == symbol_kline.end()) continue;

        const double mark = itp->second.ClosePrice;
        p.notional = std::abs(p.quantity * mark);
        const auto& instrument_spec = resolve_instrument_spec_(p.symbol);
        if (!instrument_spec.maintenance_margin_enabled) {
            p.maintenance_margin = 0.0;
            continue;
        }
        double mmr, maxLev;
        std::tie(mmr, maxLev) = get_tier_info(p.notional);
        p.maintenance_margin = p.notional * mmr;
    }

    auto snapshot = get_perp_balance();
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

    if (is_spot_instrument(pos.instrument_type)) {
        applySpotClosingCashflow(close_qty, fill_price, fee, freed_margin, freed_maint);
    }
    else {
        applyPerpClosingCashflow(realized_pnl, fee, freed_margin);
    }

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
    if (open_orders_.empty()) return;
    const bool spot_changed = cancel_spot_open_orders_(symbol);
    const bool perp_changed = cancel_perp_open_orders_(symbol);
    if (!spot_changed && !perp_changed) return;
    rebuild_open_order_index_();
    mark_open_orders_dirty_();
    ++state_version_;
}

void Account::close_position(const std::string& symbol, double price) {
    const auto& spec = resolve_instrument_spec_(symbol);
    if (spec.type == InstrumentType::Spot) {
        close_spot_position_(symbol, price);
        return;
    }
    close_perp_position_(symbol, price);
}

void Account::close_position(const std::string& symbol) {
    close_position(symbol, 0.0);
}

/// @brief Close only one side in hedge mode.
void Account::close_position(const std::string& symbol, QTrading::Dto::Trading::PositionSide position_side, double price) {
    const auto& spec = resolve_instrument_spec_(symbol);
    if (spec.type == InstrumentType::Spot) {
        if (position_side != PositionSide::Both && enable_console_output_) {
            std::cerr << "[close_position] Spot mode requires position_side=Both\n";
        }
        close_spot_position_(symbol, price);
        return;
    }

    if (!hedge_mode_) {
        if (position_side != PositionSide::Both) {
            if (enable_console_output_) {
                std::cerr << "[close_position] One-way mode requires position_side=Both\n";
            }
            return;
        }
        close_perp_position_(symbol, price);
        return;
    }

    if (position_side == PositionSide::Both) {
        close_perp_position_(symbol, price);
        return;
    }

    close_perp_position_side_(symbol, position_side, price);
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
        perp_ledger_.increase_used_margin(totalDiff);
    }
    else {
        perp_ledger_.decrease_used_margin(std::fabs(totalDiff));
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
    const auto& instrument_spec = resolve_instrument_spec_(ord.symbol);
    if (instrument_spec.type == InstrumentType::Spot) {
        processSpotOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
        return;
    }
    processPerpOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
}

Account::Policies Account::DefaultPolicies()
{
    return AccountPolicies::Default();
}
