#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <memory_resource>
#include <optional>
#include <thread>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::OrderSide;

namespace {

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

} // namespace

void Account::process_open_orders_pipeline_(double maker_fee, double taker_fee, bool& dirty, bool& open_orders_changed, bool& positions_changed)
{
    if (open_orders_.empty()) {
        return;
    }

    const FillModel fill_model{ kline_volume_split_mode_ };
    std::pmr::vector<unsigned char> keep_open_order{ &tick_memory_ };
    std::pmr::vector<Order> next_open_orders{ &tick_memory_ };
    std::vector<Order> single_leftover;
    single_leftover.reserve(1);

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
        std::pmr::vector<FillPlanEntry> fills;
        double remaining_vol{ 0.0 };
        double remaining_buy_liq{ 0.0 };
        double remaining_sell_liq{ 0.0 };
        bool has_data{ false };
        explicit SymbolPlan(std::pmr::memory_resource* mr) : fills(mr) {}
    };

    keep_open_order.assign(open_orders_.size(), true);

    std::pmr::vector<SymbolWork> symbol_work{ &tick_memory_ };
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

    const size_t symbol_count = symbol_work.size();
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t task_count = std::min(symbol_count, static_cast<size_t>(hw));
    const size_t min_parallel_symbols = 4;
    const bool use_parallel = (symbol_count >= min_parallel_symbols && task_count > 1);
    std::optional<std::pmr::synchronized_pool_resource> plan_pool;
    std::pmr::memory_resource* plan_mr = &tick_memory_;
    if (use_parallel) {
        // Use a thread-safe pool when building fill plans in parallel.
        plan_pool.emplace();
        plan_mr = &*plan_pool;
    }

    std::pmr::vector<SymbolPlan> symbol_plans{ &tick_memory_ };
    symbol_plans.reserve(symbol_count);
    for (size_t i = 0; i < symbol_count; ++i) {
        symbol_plans.emplace_back(plan_mr);
    }

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

    if (use_parallel) {
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
    else if (symbol_count > 0) {
        build_plan(0, symbol_count);
    }

    for (size_t s = 0; s < symbol_work.size(); ++s) {
        if (!symbol_plans[s].has_data) continue;
        const size_t sym_id = symbol_work[s].sym_id;
        remaining_vol_[sym_id] = symbol_plans[s].remaining_vol;
        remaining_liq_[sym_id] = { symbol_plans[s].remaining_buy_liq, symbol_plans[s].remaining_sell_liq };

        const KlineDto* kptr = (sym_id < kline_by_id_.size()) ? kline_by_id_[sym_id] : nullptr;
        const double close_price = kptr ? kptr->ClosePrice : 0.0;

        auto& fills = symbol_plans[s].fills;
        for (const auto& entry : fills) {
            Order& ord = open_orders_[entry.idx];
            const double fill_qty = entry.fill_qty;
            const double fill_price = entry.fill_price;
            const bool is_taker = entry.is_taker;
            const double order_qty = ord.quantity;
            const double order_price = ord.price;

            const double notional = fill_qty * fill_price;
            const double fee_rate = is_taker ? taker_fee : maker_fee;
            const double fee = notional * fee_rate;

            keep_open_order[entry.idx] = false;

            // Reuse single-order leftover buffer (avoids per-fill tiny vector alloc).
            single_leftover.clear();

            if (ord.closing_position_id >= 0) {
                processClosingOrder(ord, fill_qty, fill_price, fee, single_leftover);
            }
            else {
                processOpeningOrder(ord, fill_qty, fill_price, notional, fee, fee_rate, single_leftover);
            }
            positions_changed = true;

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
            fill.fee_rate = fee_rate;
            fill.closing_position_id = ord.closing_position_id;
            fill.instrument_type = ord.instrument_type;
            if (kptr) {
                update_unrealized_for_symbol_(fill.symbol, close_price);
            }
            mark_balance_dirty_();
            fill.perp_balance_snapshot = get_perp_balance();
            fill.spot_balance_snapshot = get_spot_balance();
            fill.total_cash_balance_snapshot = get_total_cash_balance();
            fill.balance_snapshot = fill.perp_balance_snapshot;
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

    if (open_orders_changed) {
        next_open_orders.clear();
        next_open_orders.reserve(open_orders_.size());
        for (size_t i = 0; i < open_orders_.size(); ++i) {
            if (keep_open_order[i]) next_open_orders.push_back(open_orders_[i]);
        }
        open_orders_.assign(next_open_orders.begin(), next_open_orders.end());
        rebuild_open_order_index_();
        mark_open_orders_dirty_();
    }
}
