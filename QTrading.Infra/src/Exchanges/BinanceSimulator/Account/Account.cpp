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

static bool is_multiple_of_step(double value, double step)
{
    if (!(std::isfinite(value) && std::isfinite(step)) || step <= 0.0) {
        return true;
    }
    const double units = value / step;
    const double nearest = std::round(units);
    return std::abs(units - nearest) <= 1e-8;
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
    strict_binance_mode_(false),
    next_order_id_(1),
    next_position_id_(1),
    policies_(DefaultPolicies()),
    tick_memory_(std::pmr::new_delete_resource())
{
    const auto cfg = validate_account_init_config(init_config);
    vip_level_ = cfg.vip_level;
    strict_binance_mode_ = cfg.strict_binance_mode;
    spot_ledger_.set_cash_balance(cfg.spot_initial_cash);
    perp_ledger_.set_wallet_balance(cfg.perp_initial_wallet);
    perp_ledger_.set_used_margin(0.0);

    open_orders_.reserve(1024);
    positions_.reserve(1024);

    open_order_index_by_id_.reserve(2048);
    open_order_client_id_count_.reserve(2048);
    pending_close_sell_qty_by_symbol_.reserve(1024);
    stp_order_ids_by_bucket_.reserve(2048);
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

void Account::set_intra_bar_path_mode(IntraBarPathMode mode)
{
    intra_bar_path_mode_ = mode;
}

Account::IntraBarPathMode Account::intra_bar_path_mode() const
{
    return intra_bar_path_mode_;
}

void Account::set_intra_bar_monte_carlo_samples(size_t samples)
{
    intra_bar_monte_carlo_samples_ = std::max<size_t>(1, samples);
}

size_t Account::intra_bar_monte_carlo_samples() const
{
    return intra_bar_monte_carlo_samples_;
}

void Account::set_intra_bar_random_seed(uint64_t seed)
{
    intra_bar_random_seed_ = seed;
}

uint64_t Account::intra_bar_random_seed() const
{
    return intra_bar_random_seed_;
}

void Account::set_limit_fill_probability_enabled(bool enable)
{
    limit_fill_probability_enabled_ = enable;
}

bool Account::is_limit_fill_probability_enabled() const
{
    return limit_fill_probability_enabled_;
}

void Account::set_limit_fill_probability_coefficients(double intercept,
    double penetration_weight,
    double size_ratio_weight,
    double taker_flow_weight,
    double volatility_weight)
{
    fill_prob_intercept_ = intercept;
    fill_prob_penetration_weight_ = penetration_weight;
    fill_prob_size_ratio_weight_ = size_ratio_weight;
    fill_prob_taker_flow_weight_ = taker_flow_weight;
    fill_prob_volatility_weight_ = volatility_weight;
}

void Account::set_market_impact_slippage_enabled(bool enable)
{
    market_impact_slippage_enabled_ = enable;
}

bool Account::is_market_impact_slippage_enabled() const
{
    return market_impact_slippage_enabled_;
}

void Account::set_market_impact_slippage_params(double b0_bps,
    double b1_bps,
    double beta,
    double b2_bps,
    double b3_bps)
{
    impact_b0_bps_ = b0_bps;
    impact_b1_bps_ = b1_bps;
    impact_beta_ = beta;
    impact_b2_bps_ = b2_bps;
    impact_b3_bps_ = b3_bps;
}

void Account::set_taker_probability_model_enabled(bool enable)
{
    taker_probability_model_enabled_ = enable;
}

bool Account::is_taker_probability_model_enabled() const
{
    return taker_probability_model_enabled_;
}

void Account::set_taker_probability_model_coefficients(double intercept,
    double same_side_flow_weight,
    double size_ratio_weight,
    double volatility_weight,
    double penetration_weight)
{
    taker_prob_intercept_ = intercept;
    taker_prob_same_side_flow_weight_ = same_side_flow_weight;
    taker_prob_size_ratio_weight_ = size_ratio_weight;
    taker_prob_volatility_weight_ = volatility_weight;
    taker_prob_penetration_weight_ = penetration_weight;
}

double Account::get_total_cash_balance() const {
    return perp_ledger_.wallet_balance() + spot_ledger_.cash_balance();
}

/// @brief Switch between one-way mode and hedge mode.
/// @param hedgeMode true to enable hedge mode (separate long/short), false for one-way.
/// @details Fails if any positions are currently open.
void Account::set_position_mode(bool hedgeMode) {
    // Disallow switching mode if there are open positions or pending orders.
    if (!positions_.empty()) {
        if (enable_console_output_) {
            std::cerr << "[set_position_mode] Cannot switch mode while positions are open.\n";
        }
        return;
    }
    if (!open_orders_.empty()) {
        if (enable_console_output_) {
            std::cerr << "[set_position_mode] Cannot switch mode while open orders exist.\n";
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

void Account::set_strict_binance_mode(bool enable)
{
    strict_binance_mode_ = enable;
}

bool Account::is_strict_binance_mode() const
{
    return strict_binance_mode_;
}

const QTrading::Dto::Trading::InstrumentSpec& Account::resolve_instrument_spec_(const std::string& symbol) const
{
    return instrument_registry_.Resolve(symbol);
}

void Account::set_instrument_type(const std::string& symbol, InstrumentType type)
{
    const auto& cur = instrument_registry_.Resolve(symbol);
    if (cur.type == type) {
        return;
    }

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
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    clear_last_order_reject_info_();
    if (quantity <= 0) {
        if (enable_console_output_) {
            std::cerr << "[place_order] Invalid quantity <= 0\n";
        }
        return reject_order_(OrderRejectInfo::Code::InvalidQuantity, "Invalid quantity <= 0");
    }

    const auto& instrument_spec = resolve_instrument_spec_(symbol);
    if (!validate_order_filters_(symbol, quantity, price, side, instrument_spec)) {
        return false;
    }
    if (!client_order_id.empty() && has_open_order_with_client_id_(client_order_id)) {
        if (enable_console_output_) {
            std::cerr << "[place_order] duplicate clientOrderId among open orders.\n";
        }
        return reject_order_(OrderRejectInfo::Code::DuplicateClientOrderId, "duplicate clientOrderId among open orders");
    }

    size_t stp_conflicts = 0;
    if (stp_mode == SelfTradePreventionMode::ExpireMaker ||
        stp_mode == SelfTradePreventionMode::ExpireBoth) {
        stp_conflicts = cancel_stp_conflicting_orders_(symbol, side, price, instrument_spec.type);
        if (stp_conflicts > 0) {
            mark_open_orders_dirty_();
            ++state_version_;
        }
    }
    else if (stp_mode == SelfTradePreventionMode::ExpireTaker) {
        stp_conflicts = count_stp_conflicting_orders_(symbol, side, price, instrument_spec.type);
    }

    if (stp_conflicts > 0) {
        if (stp_mode == SelfTradePreventionMode::ExpireTaker) {
            return reject_order_(OrderRejectInfo::Code::StpExpiredTaker, "STP expired taker order");
        }
        if (stp_mode == SelfTradePreventionMode::ExpireBoth) {
            return reject_order_(OrderRejectInfo::Code::StpExpiredBoth, "STP expired both sides");
        }
    }

    if (instrument_spec.type == InstrumentType::Spot) {
        return place_spot_order(symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode, instrument_spec);
    }

    return place_perp_order(symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode, instrument_spec);
}

bool Account::has_reducible_position_for_order_(const Order& ord) const
{
    return has_reducible_position(positions_, ord, hedge_mode_);
}

bool Account::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    return place_order(symbol, quantity, 0.0, side, position_side, reduce_only, client_order_id, stp_mode);
}

std::optional<Account::OrderRejectInfo> Account::consume_last_order_reject_info()
{
    std::optional<OrderRejectInfo> out;
    out.swap(last_order_reject_info_);
    return out;
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
    const auto prev_perp_balance = get_perp_balance();
    const auto prev_spot_balance = get_spot_balance();
    const double prev_total_cash_balance = get_total_cash_balance();
    // Reset per-tick scratch allocator before building fill buffers.
    tick_memory_.release();
    fill_events_.clear();
    if (fill_events_.capacity() < open_orders_.size()) {
        fill_events_.reserve(open_orders_.size());
    }

    bool mark_dirty = false;

    const FeeModel perp_fee_model(get_fee_rates(InstrumentType::Perp));
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
        process_open_orders_pipeline_(dirty, open_orders_changed, positions_changed);
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

    apply_perp_liquidation_(perp_fee_model.taker_fee, open_orders_changed, positions_changed);

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
        p.maintenance_margin = maintenance_margin_for_notional_(p.notional);
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

    auto balance_snapshot_equal = [](const QTrading::Dto::Account::BalanceSnapshot& a,
        const QTrading::Dto::Account::BalanceSnapshot& b) {
            return a.WalletBalance == b.WalletBalance &&
                a.UnrealizedPnl == b.UnrealizedPnl &&
                a.MarginBalance == b.MarginBalance &&
                a.PositionInitialMargin == b.PositionInitialMargin &&
                a.OpenOrderInitialMargin == b.OpenOrderInitialMargin &&
                a.AvailableBalance == b.AvailableBalance &&
                a.MaintenanceMargin == b.MaintenanceMargin &&
                a.Equity == b.Equity;
        };

    const auto cur_perp_balance = get_perp_balance();
    const auto cur_spot_balance = get_spot_balance();
    const double cur_total_cash_balance = get_total_cash_balance();
    const bool balance_changed =
        !balance_snapshot_equal(prev_perp_balance, cur_perp_balance) ||
        !balance_snapshot_equal(prev_spot_balance, cur_spot_balance) ||
        (prev_total_cash_balance != cur_total_cash_balance);

    dirty = dirty || open_orders_changed || positions_changed || balance_changed;

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
    const double n = std::max(0.0, notional);
    for (const auto& tier : margin_tiers) {
        if (n <= tier.notional_upper) {
            return std::make_tuple(tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    return std::make_tuple(margin_tiers.front().maintenance_margin_rate, margin_tiers.front().max_leverage);
}

double Account::maintenance_margin_for_notional_(double notional) const
{
    const double n = std::max(0.0, notional);
    if (margin_tiers.empty()) {
        return 0.0;
    }

    // Binance bracket-style cumulative deduction to keep piecewise function continuous.
    double deduction = 0.0;
    for (size_t i = 0; i < margin_tiers.size(); ++i) {
        const auto& tier = margin_tiers[i];
        if (i > 0) {
            const auto& prev = margin_tiers[i - 1];
            deduction += prev.notional_upper * (tier.maintenance_margin_rate - prev.maintenance_margin_rate);
        }

        if (n <= tier.notional_upper) {
            const double mm = n * tier.maintenance_margin_rate - deduction;
            return std::max(0.0, mm);
        }
    }

    const auto& back = margin_tiers.back();
    const double mm = n * back.maintenance_margin_rate - deduction;
    return std::max(0.0, mm);
}

/// @brief Get maker and taker fee rates by instrument type and VIP level.
/// @return Tuple(maker_fee_rate, taker_fee_rate).
std::tuple<double, double> Account::get_fee_rates(InstrumentType instrument_type) const {
    if (instrument_type == InstrumentType::Spot) {
        if (policies_.spot_fee_rates) {
            return policies_.spot_fee_rates(vip_level_);
        }
        // Backward compatibility: if caller injects only `fee_rates`, reuse it for spot too.
        if (policies_.fee_rates) {
            return policies_.fee_rates(vip_level_);
        }
        auto it_spot = spot_vip_fee_rates.find(vip_level_);
        if (it_spot != spot_vip_fee_rates.end()) {
            return std::make_tuple(it_spot->second.maker_fee_rate, it_spot->second.taker_fee_rate);
        }
        return std::make_tuple(spot_vip_fee_rates.at(0).maker_fee_rate, spot_vip_fee_rates.at(0).taker_fee_rate);
    }

    if (policies_.fee_rates) {
        return policies_.fee_rates(vip_level_);
    }
    auto it = vip_fee_rates.find(vip_level_);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
}

bool Account::validate_order_filters_(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    const QTrading::Dto::Trading::InstrumentSpec& instrument_spec)
{
    const bool is_market = (price <= 0.0);

    if (!is_market) {
        if (instrument_spec.min_price > 0.0 && price + 1e-12 < instrument_spec.min_price) {
            if (enable_console_output_) {
                std::cerr << "[place_order] PRICE_FILTER reject: below min_price.\n";
            }
            return reject_order_(OrderRejectInfo::Code::PriceFilterBelowMin, "PRICE_FILTER reject: below min_price");
        }
        if (instrument_spec.max_price > 0.0 && price - 1e-12 > instrument_spec.max_price) {
            if (enable_console_output_) {
                std::cerr << "[place_order] PRICE_FILTER reject: above max_price.\n";
            }
            return reject_order_(OrderRejectInfo::Code::PriceFilterAboveMax, "PRICE_FILTER reject: above max_price");
        }
        if (instrument_spec.price_tick_size > 0.0 && !is_multiple_of_step(price, instrument_spec.price_tick_size)) {
            if (enable_console_output_) {
                std::cerr << "[place_order] PRICE_FILTER reject: invalid tick size step.\n";
            }
            return reject_order_(OrderRejectInfo::Code::PriceFilterInvalidTick, "PRICE_FILTER reject: invalid tick size step");
        }
    }

    const double min_qty = is_market && instrument_spec.market_min_qty > 0.0
        ? instrument_spec.market_min_qty
        : instrument_spec.min_qty;
    const double max_qty = is_market && instrument_spec.market_max_qty > 0.0
        ? instrument_spec.market_max_qty
        : instrument_spec.max_qty;
    const double qty_step = is_market && instrument_spec.market_qty_step_size > 0.0
        ? instrument_spec.market_qty_step_size
        : instrument_spec.qty_step_size;

    if (min_qty > 0.0 && quantity + 1e-12 < min_qty) {
        if (enable_console_output_) {
            std::cerr << "[place_order] LOT_SIZE reject: below min_qty.\n";
        }
        return reject_order_(OrderRejectInfo::Code::LotSizeBelowMinQty, "LOT_SIZE reject: below min_qty");
    }
    if (max_qty > 0.0 && quantity - 1e-12 > max_qty) {
        if (enable_console_output_) {
            std::cerr << "[place_order] LOT_SIZE reject: above max_qty.\n";
        }
        return reject_order_(OrderRejectInfo::Code::LotSizeAboveMaxQty, "LOT_SIZE reject: above max_qty");
    }
    if (qty_step > 0.0 && !is_multiple_of_step(quantity, qty_step)) {
        if (enable_console_output_) {
            std::cerr << "[place_order] LOT_SIZE reject: invalid qty step.\n";
        }
        return reject_order_(OrderRejectInfo::Code::LotSizeInvalidStep, "LOT_SIZE reject: invalid qty step");
    }

    double notional_est = 0.0;
    if (!is_market) {
        notional_est = quantity * price;
    }
    else {
        auto it = symbol_id_by_name_.find(symbol);
        if (it != symbol_id_by_name_.end()) {
            const size_t sym_id = it->second;
            if (sym_id < last_mark_price_by_id_.size()) {
                const double mark = last_mark_price_by_id_[sym_id];
                if (std::isfinite(mark) && mark > 0.0) {
                    notional_est = quantity * mark * (1.0 + std::max(0.0, market_slippage_buffer_));
                }
            }
        }
    }

    const bool require_notional = (instrument_spec.min_notional > 0.0 || instrument_spec.max_notional > 0.0);
    if (require_notional) {
        if (!(notional_est > 0.0)) {
            if (enable_console_output_) {
                std::cerr << "[place_order] NOTIONAL reject: no reference price for notional estimation.\n";
            }
            return reject_order_(OrderRejectInfo::Code::NotionalNoReferencePrice, "NOTIONAL reject: no reference price for notional estimation");
        }
        if (instrument_spec.min_notional > 0.0 && notional_est + 1e-12 < instrument_spec.min_notional) {
            if (enable_console_output_) {
                std::cerr << "[place_order] NOTIONAL reject: below min_notional.\n";
            }
            return reject_order_(OrderRejectInfo::Code::NotionalBelowMin, "NOTIONAL reject: below min_notional");
        }
        if (instrument_spec.max_notional > 0.0 && notional_est - 1e-12 > instrument_spec.max_notional) {
            if (enable_console_output_) {
                std::cerr << "[place_order] NOTIONAL reject: above max_notional.\n";
            }
            return reject_order_(OrderRejectInfo::Code::NotionalAboveMax, "NOTIONAL reject: above max_notional");
        }
    }

    if (!is_market) {
        auto it = symbol_id_by_name_.find(symbol);
        double ref_price = 0.0;
        if (it != symbol_id_by_name_.end()) {
            const size_t sym_id = it->second;
            if (sym_id < last_mark_price_by_id_.size()) {
                const double mark = last_mark_price_by_id_[sym_id];
                if (std::isfinite(mark) && mark > 0.0) {
                    ref_price = mark;
                }
            }
        }

        if (ref_price > 0.0) {
            double up = instrument_spec.percent_price_multiplier_up;
            double down = instrument_spec.percent_price_multiplier_down;
            if (instrument_spec.percent_price_by_side) {
                if (side == OrderSide::Buy) {
                    up = instrument_spec.bid_multiplier_up;
                    down = instrument_spec.bid_multiplier_down;
                }
                else {
                    up = instrument_spec.ask_multiplier_up;
                    down = instrument_spec.ask_multiplier_down;
                }
            }

            if (up > 0.0 && price - 1e-12 > ref_price * up) {
                if (enable_console_output_) {
                    std::cerr << "[place_order] PERCENT_PRICE reject: above multiplier up bound.\n";
                }
                return reject_order_(OrderRejectInfo::Code::PercentPriceAboveBound, "PERCENT_PRICE reject: above multiplier up bound");
            }
            if (down > 0.0 && price + 1e-12 < ref_price * down) {
                if (enable_console_output_) {
                    std::cerr << "[place_order] PERCENT_PRICE reject: below multiplier down bound.\n";
                }
                return reject_order_(OrderRejectInfo::Code::PercentPriceBelowBound, "PERCENT_PRICE reject: below multiplier down bound");
            }
        }
    }

    return true;
}

void Account::clear_last_order_reject_info_()
{
    last_order_reject_info_.reset();
}

bool Account::reject_order_(OrderRejectInfo::Code code, std::string message)
{
    last_order_reject_info_ = OrderRejectInfo{ code, std::move(message) };
    return false;
}

bool Account::has_open_order_with_client_id_(const std::string& client_order_id) const
{
    if (client_order_id.empty()) {
        return false;
    }
    auto it = open_order_client_id_count_.find(client_order_id);
    return it != open_order_client_id_count_.end() && it->second > 0;
}

size_t Account::count_stp_conflicting_orders_(const std::string& symbol,
    OrderSide incoming_side,
    double incoming_price,
    InstrumentType instrument_type) const
{
    auto it_sym = symbol_id_by_name_.find(symbol);
    if (it_sym == symbol_id_by_name_.end()) {
        return 0;
    }
    const OrderSide resting_side = (incoming_side == OrderSide::Buy) ? OrderSide::Sell : OrderSide::Buy;
    const StpBucketKey key{ it_sym->second, instrument_type, resting_side };
    auto it_bucket = stp_order_ids_by_bucket_.find(key);
    if (it_bucket == stp_order_ids_by_bucket_.end()) {
        return 0;
    }

    auto is_cross = [&](const Order& resting) {
        if (incoming_price <= 0.0 || resting.price <= 0.0) {
            return true;
        }
        if (incoming_side == OrderSide::Buy) {
            return incoming_price + 1e-12 >= resting.price;
        }
        return incoming_price <= resting.price + 1e-12;
    };

    size_t count = 0;
    for (int order_id : it_bucket->second) {
        auto it_idx = open_order_index_by_id_.find(order_id);
        if (it_idx == open_order_index_by_id_.end()) {
            continue;
        }
        const Order& o = open_orders_[it_idx->second];
        if (is_cross(o)) {
            ++count;
        }
    }
    return count;
}

size_t Account::cancel_stp_conflicting_orders_(const std::string& symbol,
    OrderSide incoming_side,
    double incoming_price,
    InstrumentType instrument_type)
{
    auto it_sym = symbol_id_by_name_.find(symbol);
    if (it_sym == symbol_id_by_name_.end()) {
        return 0;
    }
    const OrderSide resting_side = (incoming_side == OrderSide::Buy) ? OrderSide::Sell : OrderSide::Buy;
    const StpBucketKey key{ it_sym->second, instrument_type, resting_side };
    auto it_bucket = stp_order_ids_by_bucket_.find(key);
    if (it_bucket == stp_order_ids_by_bucket_.end()) {
        return 0;
    }

    auto is_cross = [&](const Order& resting) {
        if (incoming_price <= 0.0 || resting.price <= 0.0) {
            return true;
        }
        if (incoming_side == OrderSide::Buy) {
            return incoming_price + 1e-12 >= resting.price;
        }
        return incoming_price <= resting.price + 1e-12;
    };

    std::vector<char> remove_mask(open_orders_.size(), 0);
    size_t removed = 0;
    for (int order_id : it_bucket->second) {
        auto it_idx = open_order_index_by_id_.find(order_id);
        if (it_idx == open_order_index_by_id_.end()) {
            continue;
        }
        const size_t idx = it_idx->second;
        if (idx >= open_orders_.size()) {
            continue;
        }
        if (remove_mask[idx]) {
            continue;
        }
        if (is_cross(open_orders_[idx])) {
            remove_mask[idx] = 1;
            ++removed;
        }
    }

    if (removed == 0) {
        return 0;
    }

    size_t write = 0;
    for (size_t read = 0; read < open_orders_.size(); ++read) {
        if (remove_mask[read]) {
            continue;
        }
        if (write != read) {
            open_orders_[write] = std::move(open_orders_[read]);
        }
        ++write;
    }
    open_orders_.resize(write);
    rebuild_open_order_index_();
    return removed;
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
        double maxLev = 1.0;
        std::tie(std::ignore, maxLev) = get_tier_info(p.notional);
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
        newMaint[i] = maintenance_margin_for_notional_(p.notional);
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
