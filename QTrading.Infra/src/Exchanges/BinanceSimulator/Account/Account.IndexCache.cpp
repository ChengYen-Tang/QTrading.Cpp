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
        last_mark_price_by_id_.resize(need);
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

void Account::rebuild_open_order_index_()
{
    open_order_index_by_id_.clear();
    open_order_index_by_id_.reserve(open_orders_.size());
    for (size_t i = 0; i < open_orders_.size(); ++i) {
        open_order_index_by_id_[open_orders_[i].id] = i;
    }
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
