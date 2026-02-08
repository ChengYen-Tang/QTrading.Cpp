#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <memory_resource>
#include <optional>
#include <thread>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;

namespace {

static bool is_limit_order_marketable_at_open(const Order& ord, const KlineDto& k)
{
    if (ord.price <= 0.0) {
        return true;
    }
    if (ord.side == OrderSide::Buy) {
        return k.OpenPrice <= ord.price;
    }
    return k.OpenPrice >= ord.price;
}

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

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

std::pair<bool, bool> Account::evaluate_can_fill_and_taker_(const Order& ord, const KlineDto& k) const
{
    const FillModel fill_model{ kline_volume_split_mode_ };
    const auto base = policies_.can_fill_and_taker
        ? policies_.can_fill_and_taker(ord, k)
        : fill_model.can_fill_and_taker(ord, k);

    if (!base.first) {
        return base;
    }

    if (ord.price <= 0.0) {
        return { true, true };
    }

    if (intra_bar_path_mode_ == IntraBarPathMode::LegacyCloseHeuristic) {
        return base;
    }

    // In path mode, a limit order is taker only when marketable at bar open.
    return { true, is_limit_order_marketable_at_open(ord, k) };
}

double Account::limit_fill_probability_(const Order& ord, const KlineDto& k, bool is_taker) const
{
    if (ord.price <= 0.0 || is_taker || !limit_fill_probability_enabled_) {
        return 1.0;
    }

    const double vol = std::max(1e-12, k.Volume);
    const double range = std::max(1e-12, k.HighPrice - k.LowPrice);
    const double open_abs = std::max(1e-12, std::abs(k.OpenPrice));
    const double size_ratio = std::clamp(ord.quantity / vol, 0.0, 10.0);
    const double volatility = std::clamp(range / open_abs, 0.0, 10.0);

    double penetration = 0.0;
    if (ord.side == OrderSide::Buy) {
        penetration = (ord.price - k.LowPrice) / range;
    }
    else {
        penetration = (k.HighPrice - ord.price) / range;
    }
    penetration = std::clamp(penetration, 0.0, 2.0);

    double taker_buy_ratio = 0.5;
    if (k.TakerBuyBaseVolume > 0.0) {
        taker_buy_ratio = std::clamp(k.TakerBuyBaseVolume / vol, 0.0, 1.0);
    }
    const double favorable_flow = (ord.side == OrderSide::Buy) ? (1.0 - taker_buy_ratio) : taker_buy_ratio;
    const double flow_centered = (favorable_flow - 0.5) * 2.0;

    const double z = fill_prob_intercept_
        + fill_prob_penetration_weight_ * penetration
        - fill_prob_size_ratio_weight_ * size_ratio
        + fill_prob_taker_flow_weight_ * flow_centered
        + fill_prob_volatility_weight_ * volatility;

    const double p = 1.0 / (1.0 + std::exp(-z));
    if (!std::isfinite(p)) {
        return 1.0;
    }
    return std::clamp(p, 0.0, 1.0);
}

std::pair<double, double> Account::apply_market_impact_slippage_(const Order& ord,
    const KlineDto& k,
    double base_fill_price,
    double fill_qty) const
{
    if (!market_impact_slippage_enabled_ || fill_qty <= 0.0 || base_fill_price <= 0.0) {
        return { base_fill_price, 0.0 };
    }

    const double vol = std::max(1e-12, k.Volume);
    const double open_abs = std::max(1e-12, std::abs(k.OpenPrice));
    const double size_ratio = std::clamp(fill_qty / vol, 0.0, 10.0);
    const double volatility = std::clamp(std::abs(k.ClosePrice - k.OpenPrice) / open_abs, 0.0, 10.0);
    const double spread_proxy = std::clamp((k.HighPrice - k.LowPrice) / open_abs, 0.0, 10.0);
    const double beta = std::max(0.0, impact_beta_);
    const double impact_bps = impact_b0_bps_
        + impact_b1_bps_ * std::pow(size_ratio, beta)
        + impact_b2_bps_ * volatility
        + impact_b3_bps_ * spread_proxy;
    const double impact_pct = std::max(0.0, impact_bps) / 10000.0;

    double impacted = base_fill_price;
    if (ord.side == OrderSide::Buy) {
        impacted = std::min(k.HighPrice, base_fill_price * (1.0 + impact_pct));
        if (ord.price > 0.0) {
            impacted = std::min(ord.price, impacted);
        }
    }
    else {
        impacted = std::max(k.LowPrice, base_fill_price * (1.0 - impact_pct));
        if (ord.price > 0.0) {
            impacted = std::max(ord.price, impacted);
        }
    }

    return { impacted, std::max(0.0, impact_bps) };
}

double Account::taker_probability_(const Order& ord, const KlineDto& k, bool base_is_taker) const
{
    if (base_is_taker || ord.price <= 0.0) {
        return 1.0;
    }
    if (!taker_probability_model_enabled_) {
        return base_is_taker ? 1.0 : 0.0;
    }

    const double vol = std::max(1e-12, k.Volume);
    const double range = std::max(1e-12, k.HighPrice - k.LowPrice);
    const double open_abs = std::max(1e-12, std::abs(k.OpenPrice));
    const double size_ratio = std::clamp(ord.quantity / vol, 0.0, 10.0);
    const double volatility = std::clamp(range / open_abs, 0.0, 10.0);

    double penetration = 0.0;
    if (ord.side == OrderSide::Buy) {
        penetration = std::clamp((ord.price - k.LowPrice) / range, 0.0, 2.0);
    }
    else {
        penetration = std::clamp((k.HighPrice - ord.price) / range, 0.0, 2.0);
    }

    double taker_buy_ratio = 0.5;
    if (k.TakerBuyBaseVolume > 0.0) {
        taker_buy_ratio = std::clamp(k.TakerBuyBaseVolume / vol, 0.0, 1.0);
    }
    const double same_side_flow = (ord.side == OrderSide::Buy) ? taker_buy_ratio : (1.0 - taker_buy_ratio);
    const double flow_centered = (same_side_flow - 0.5) * 2.0;

    const double z = taker_prob_intercept_
        + taker_prob_same_side_flow_weight_ * flow_centered
        + taker_prob_size_ratio_weight_ * size_ratio
        + taker_prob_volatility_weight_ * volatility
        - taker_prob_penetration_weight_ * penetration;

    const double p = 1.0 / (1.0 + std::exp(-z));
    if (!std::isfinite(p)) {
        return 0.0;
    }
    return std::clamp(p, 0.0, 1.0);
}

void Account::process_open_orders_pipeline_(bool& dirty, bool& open_orders_changed, bool& positions_changed)
{
    if (open_orders_.empty()) {
        return;
    }

    const auto perp_fee_rates = get_fee_rates(InstrumentType::Perp);
    const auto spot_fee_rates = get_fee_rates(InstrumentType::Spot);
    const double perp_maker_fee = std::get<0>(perp_fee_rates);
    const double perp_taker_fee = std::get<1>(perp_fee_rates);
    const double spot_maker_fee = std::get<0>(spot_fee_rates);
    const double spot_taker_fee = std::get<1>(spot_fee_rates);

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
        double taker_probability;
        double fill_probability;
        double impact_slippage_bps;
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

            const bool use_path_model =
                (!use_dir_liq && intra_bar_path_mode_ != IntraBarPathMode::LegacyCloseHeuristic);

            if (!use_path_model) {
                const size_t max_checks = max_match_orders_per_symbol_;
                size_t checked = 0;
                for (size_t idx : indices) {
                    if (max_checks > 0 && checked >= max_checks) {
                        break;
                    }
                    if (total_vol <= 0.0) break;

                    const auto [can_fill, base_is_taker] = evaluate_can_fill_and_taker_(open_orders_[idx], k);
                    if (!can_fill) continue;

                    const Order& ord = open_orders_[idx];
                    const double taker_prob = taker_probability_(ord, k, base_is_taker);
                    const bool is_taker = taker_prob >= 0.5;
                    const double fill_prob = limit_fill_probability_(ord, k, is_taker);
                    const double desired_qty = ord.quantity * fill_prob;
                    double fill_qty = 0.0;
                    if (use_dir_liq) {
                        const bool order_is_buy = (ord.side == OrderSide::Buy);
                        double& dir_liq = order_is_buy ? sell_liq : buy_liq;
                        if (dir_liq <= 0.0) continue;
                        fill_qty = std::min({ desired_qty, total_vol, dir_liq });
                        if (fill_qty < 1e-8) continue;
                        total_vol -= fill_qty;
                        dir_liq -= fill_qty;
                    }
                    else {
                        fill_qty = std::min(desired_qty, total_vol);
                        if (fill_qty < 1e-8) continue;
                        total_vol -= fill_qty;
                    }

                    const double base_fill_price = policies_.execution_price
                        ? policies_.execution_price(ord, k, market_execution_slippage_, limit_execution_slippage_)
                        : PriceExecutionModel{ market_execution_slippage_, limit_execution_slippage_ }.execution_price(ord, k);
                    const auto [fill_price, impact_bps] = apply_market_impact_slippage_(ord, k, base_fill_price, fill_qty);

                    plan.fills.push_back(FillPlanEntry{ idx, fill_qty, fill_price, is_taker, taker_prob, fill_prob, impact_bps });
                    ++checked;
                }
            }
            else {
                const size_t n = indices.size();
                std::vector<double> fill_a(n, 0.0);
                std::vector<double> fill_b(n, 0.0);
                std::vector<unsigned char> can_fill(n, 0);
                std::vector<unsigned char> is_taker(n, 0);
                std::vector<unsigned char> marketable_open(n, 0);
                std::vector<double> taker_prob(n, 0.0);
                std::vector<double> fill_prob(n, 1.0);
                std::vector<double> desired_qty(n, 0.0);

                for (size_t local = 0; local < n; ++local) {
                    const Order& ord = open_orders_[indices[local]];
                    const auto [ok, base_taker] = evaluate_can_fill_and_taker_(ord, k);
                    if (!ok) {
                        continue;
                    }
                    const double p_taker = taker_probability_(ord, k, base_taker);
                    const bool taker = p_taker >= 0.5;
                    can_fill[local] = 1;
                    is_taker[local] = taker ? 1 : 0;
                    taker_prob[local] = p_taker;
                    if (ord.price > 0.0 && taker) {
                        marketable_open[local] = 1;
                    }
                    fill_prob[local] = limit_fill_probability_(ord, k, taker);
                    desired_qty[local] = ord.quantity * fill_prob[local];
                }

                auto simulate_path = [&](bool sell_first, std::vector<double>& out) {
                    std::fill(out.begin(), out.end(), 0.0);
                    double rem = total_vol_init;
                    const size_t max_checks = max_match_orders_per_symbol_;
                    size_t checked = 0;
                    const int first_phase = sell_first ? 1 : 2;
                    const int second_phase = sell_first ? 2 : 1;

                    auto run_phase = [&](int phase) {
                        for (size_t local = 0; local < n; ++local) {
                            if (rem <= 0.0) {
                                return;
                            }
                            if (max_checks > 0 && checked >= max_checks) {
                                return;
                            }
                            if (can_fill[local] == 0) {
                                continue;
                            }

                            const Order& ord = open_orders_[indices[local]];
                            int order_phase = 0;
                            if (ord.price <= 0.0 || marketable_open[local] != 0) {
                                order_phase = 0;
                            }
                            else {
                                order_phase = (ord.side == OrderSide::Sell) ? 1 : 2;
                            }
                            if (order_phase != phase) {
                                continue;
                            }

                            const double qty = std::min(desired_qty[local], rem);
                            if (qty < 1e-8) {
                                continue;
                            }
                            out[local] = qty;
                            rem -= qty;
                            ++checked;
                        }
                    };

                    run_phase(0);
                    run_phase(first_phase);
                    run_phase(second_phase);
                };

                simulate_path(true, fill_a);   // O -> H -> L -> C
                simulate_path(false, fill_b);  // O -> L -> H -> C

                double weight_a = 0.5;
                if (intra_bar_path_mode_ == IntraBarPathMode::MonteCarloPath) {
                    const size_t samples = std::max<size_t>(1, intra_bar_monte_carlo_samples_);
                    uint64_t state = intra_bar_random_seed_ ^ (static_cast<uint64_t>(sym_id) * 0x9E3779B97F4A7C15ull)
                        ^ static_cast<uint64_t>(k.Timestamp);
                    size_t pick_a = 0;
                    for (size_t s = 0; s < samples; ++s) {
                        state = splitmix64(state + static_cast<uint64_t>(s));
                        if ((state & 1ull) != 0ull) {
                            ++pick_a;
                        }
                    }
                    weight_a = static_cast<double>(pick_a) / static_cast<double>(samples);
                }

                double consumed = 0.0;
                for (size_t local = 0; local < n; ++local) {
                    if (can_fill[local] == 0) {
                        continue;
                    }
                    const double qty = fill_a[local] * weight_a + fill_b[local] * (1.0 - weight_a);
                    if (qty < 1e-8) {
                        continue;
                    }
                    const size_t idx = indices[local];
                    const Order& ord = open_orders_[idx];
                    const double base_fill_price = policies_.execution_price
                        ? policies_.execution_price(ord, k, market_execution_slippage_, limit_execution_slippage_)
                        : PriceExecutionModel{ market_execution_slippage_, limit_execution_slippage_ }.execution_price(ord, k);
                    const auto [fill_price, impact_bps] = apply_market_impact_slippage_(ord, k, base_fill_price, qty);
                    plan.fills.push_back(FillPlanEntry{
                        idx,
                        qty,
                        fill_price,
                        is_taker[local] != 0,
                        taker_prob[local],
                        fill_prob[local],
                        impact_bps
                    });
                    consumed += qty;
                }

                total_vol = std::max(0.0, total_vol_init - consumed);
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
            const double taker_probability = entry.taker_probability;
            const double fill_probability = entry.fill_probability;
            const double impact_slippage_bps = entry.impact_slippage_bps;
            const double order_qty = ord.quantity;
            const double order_price = ord.price;

            const double notional = fill_qty * fill_price;
            const bool is_spot = (ord.instrument_type == InstrumentType::Spot);
            const double maker_rate = is_spot ? spot_maker_fee : perp_maker_fee;
            const double taker_rate = is_spot ? spot_taker_fee : perp_taker_fee;
            const double fee_rate = maker_rate * (1.0 - taker_probability) + taker_rate * taker_probability;
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
            fill.taker_probability = taker_probability;
            fill.fill_probability = fill_probability;
            fill.impact_slippage_bps = impact_slippage_bps;
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
