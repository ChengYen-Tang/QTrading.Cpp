#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>

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
        mark_kline_point_by_id_.resize(need);
        index_kline_point_by_id_.resize(need);
        last_mark_price_by_id_.resize(need);
        last_trade_price_by_id_.resize(need);
        last_index_price_by_id_.resize(need);
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

void Account::ensure_market_symbol_id_mapping_(
    const std::shared_ptr<const std::vector<std::string>>& symbols,
    size_t by_id_count)
{
    const bool symbols_changed = (symbols != market_symbols_by_id_);
    const bool size_changed = (internal_symbol_id_by_market_id_.size() != by_id_count);
    if (!symbols_changed && !size_changed) {
        return;
    }

    market_symbols_by_id_ = symbols;
    internal_symbol_id_by_market_id_.assign(by_id_count, 0);
    for (size_t market_id = 0; market_id < by_id_count; ++market_id) {
        size_t internal_id = market_id;
        if (symbols && market_id < symbols->size()) {
            internal_id = get_symbol_id_((*symbols)[market_id]);
        }
        else {
            ensure_symbol_capacity_(internal_id);
        }
        internal_symbol_id_by_market_id_[market_id] = internal_id;
    }
}

void Account::rebuild_open_order_index_()
{
    open_order_index_by_id_.clear();
    open_order_client_id_count_.clear();
    pending_close_sell_qty_by_symbol_.clear();
    spot_open_buy_limit_notional_total_ = 0.0;
    spot_open_buy_market_qty_by_symbol_.clear();
    stp_order_ids_by_bucket_.clear();
    open_order_index_by_id_.reserve(open_orders_.size());
    open_order_client_id_count_.reserve(open_orders_.size());
    pending_close_sell_qty_by_symbol_.reserve(open_orders_.size());
    spot_open_buy_market_qty_by_symbol_.reserve(open_orders_.size());
    stp_order_ids_by_bucket_.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        index_open_order_entry_(open_orders_[i], i);
    }
}

bool Account::filter_open_orders_(const std::function<bool(const Order&)>& remove_pred)
{
    if (open_orders_.empty()) {
        return false;
    }

    open_order_index_by_id_.clear();
    open_order_client_id_count_.clear();
    pending_close_sell_qty_by_symbol_.clear();
    spot_open_buy_limit_notional_total_ = 0.0;
    spot_open_buy_market_qty_by_symbol_.clear();
    stp_order_ids_by_bucket_.clear();
    open_order_index_by_id_.reserve(open_orders_.size());
    open_order_client_id_count_.reserve(open_orders_.size());
    pending_close_sell_qty_by_symbol_.reserve(open_orders_.size());
    spot_open_buy_market_qty_by_symbol_.reserve(open_orders_.size());
    stp_order_ids_by_bucket_.reserve(open_orders_.size());

    bool changed = false;
    size_t write = 0;
    for (size_t read = 0; read < open_orders_.size(); ++read) {
        if (remove_pred(open_orders_[read])) {
            changed = true;
            continue;
        }
        if (write != read) {
            open_orders_[write] = std::move(open_orders_[read]);
        }
        index_open_order_entry_(open_orders_[write], write);
        ++write;
    }

    if (changed) {
        open_orders_.resize(write);
    }
    return changed;
}

void Account::append_open_order_(Order ord)
{
    const size_t idx = open_orders_.size();
    open_orders_.push_back(std::move(ord));
    index_open_order_entry_(open_orders_.back(), idx);
}

void Account::index_open_order_entry_(const Order& ord, size_t idx)
{
    open_order_index_by_id_[ord.id] = idx;

    if (!ord.client_order_id.empty()) {
        ++open_order_client_id_count_[ord.client_order_id];
    }

    if (is_pending_close_sell_order_(ord)) {
        pending_close_sell_qty_by_symbol_[ord.symbol] += ord.quantity;
    }
    if (is_spot_open_buy_reserve_order_(ord)) {
        if (ord.price > 0.0) {
            spot_open_buy_limit_notional_total_ += ord.quantity * ord.price;
        }
        else {
            spot_open_buy_market_qty_by_symbol_[ord.symbol] += ord.quantity;
        }
    }

    const size_t sym_id = get_symbol_id_(ord.symbol);
    StpBucketKey key{ sym_id, ord.instrument_type, ord.side };
    stp_order_ids_by_bucket_[key].push_back(ord.id);
}

bool Account::is_pending_close_sell_order_(const Order& ord)
{
    return ord.side == QTrading::Dto::Trading::OrderSide::Sell &&
        ord.quantity > 1e-8 &&
        (ord.closing_position_id >= 0 || ord.reduce_only);
}

bool Account::is_spot_open_buy_reserve_order_(const Order& ord)
{
    return ord.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot &&
        ord.side == QTrading::Dto::Trading::OrderSide::Buy &&
        ord.closing_position_id < 0 &&
        !ord.reduce_only &&
        ord.quantity > 1e-8;
}

double Account::pending_close_sell_qty_for_symbol_(const std::string& symbol) const
{
    auto it = pending_close_sell_qty_by_symbol_.find(symbol);
    return it == pending_close_sell_qty_by_symbol_.end() ? 0.0 : it->second;
}

void Account::rebuild_position_index_()
{
    position_index_by_id_.clear();
    position_index_by_id_.reserve(positions_.size());
    position_indices_by_symbol_.clear();
    position_indices_by_symbol_.reserve(positions_.size());
    for (size_t i = 0; i < positions_.size(); ++i) {
        position_index_by_id_[positions_[i].id] = i;
        position_indices_by_symbol_[positions_[i].symbol].push_back(i);
    }
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

        const bool A_is_buy = (A.side == QTrading::Dto::Trading::OrderSide::Buy);
        const bool B_is_buy = (B.side == QTrading::Dto::Trading::OrderSide::Buy);

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
