#include "Exchanges/BinanceSimulator/Output/SnapshotBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {
namespace {
constexpr uint64_t kUnsetSnapshotTimestamp = std::numeric_limits<uint64_t>::max();
constexpr int32_t kReferencePriceSourceNone = static_cast<int32_t>(Contracts::ReferencePriceSource::None);

double compute_spot_inventory_value(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::SnapshotState& snapshot_state,
    const State::StepKernelState& step_state)
{
    if (!snapshot_state.symbols_shared) {
        return 0.0;
    }

    double total = 0.0;
    const size_t symbol_count = std::min(
        step_state.symbols.size(),
        runtime_state.spot_inventory_qty_by_symbol.size());
    for (size_t symbol_id = 0; symbol_id < symbol_count; ++symbol_id) {
        const double qty = runtime_state.spot_inventory_qty_by_symbol[symbol_id];
        if (!(qty > 0.0)) {
            continue;
        }
        double price =
            symbol_id < runtime_state.spot_inventory_entry_price_by_symbol.size()
                ? runtime_state.spot_inventory_entry_price_by_symbol[symbol_id]
                : 0.0;
        if (symbol_id < snapshot_state.has_last_trade_price_by_symbol.size() &&
            symbol_id < snapshot_state.last_trade_price_by_symbol.size() &&
            snapshot_state.has_last_trade_price_by_symbol[symbol_id] != 0) {
            price = snapshot_state.last_trade_price_by_symbol[symbol_id];
        }
        total += qty * price;
    }
    return total;
}

void materialize_price_row(
    const State::SnapshotState& snapshot_state,
    const std::vector<std::string>& symbols,
    size_t symbol_id,
    Contracts::StatusPriceSnapshot& out_row)
{
    if (out_row.symbol != symbols[symbol_id]) {
        out_row.symbol = symbols[symbol_id];
    }

    if (symbol_id < snapshot_state.price_rows_by_symbol.size()) {
        const auto& row = snapshot_state.price_rows_by_symbol[symbol_id];
        out_row.price = row.price;
        out_row.has_price = row.has_price;
        out_row.trade_price = row.trade_price;
        out_row.has_trade_price = row.has_trade_price;
        out_row.mark_price = row.mark_price;
        out_row.has_mark_price = row.has_mark_price;
        out_row.mark_price_source = row.mark_price_source;
        out_row.index_price = row.index_price;
        out_row.has_index_price = row.has_index_price;
        out_row.index_price_source = row.index_price_source;
        return;
    }

    out_row.price = 0.0;
    out_row.has_price = false;
    out_row.trade_price = 0.0;
    out_row.has_trade_price = false;
    out_row.mark_price = 0.0;
    out_row.has_mark_price = false;
    out_row.mark_price_source = kReferencePriceSourceNone;
    out_row.index_price = 0.0;
    out_row.has_index_price = false;
    out_row.index_price_source = kReferencePriceSourceNone;
    if (symbol_id < snapshot_state.has_last_trade_price_by_symbol.size() &&
        snapshot_state.has_last_trade_price_by_symbol[symbol_id] != 0 &&
        symbol_id < snapshot_state.last_trade_price_by_symbol.size()) {
        out_row.trade_price = snapshot_state.last_trade_price_by_symbol[symbol_id];
        out_row.has_trade_price = true;
        out_row.price = out_row.trade_price;
        out_row.has_price = true;
    }
    if (symbol_id < snapshot_state.has_last_mark_price_by_symbol.size() &&
        snapshot_state.has_last_mark_price_by_symbol[symbol_id] != 0 &&
        symbol_id < snapshot_state.last_mark_price_by_symbol.size()) {
        out_row.mark_price = snapshot_state.last_mark_price_by_symbol[symbol_id];
        out_row.has_mark_price = true;
        if (symbol_id < snapshot_state.last_mark_price_source_by_symbol.size()) {
            out_row.mark_price_source = snapshot_state.last_mark_price_source_by_symbol[symbol_id];
        }
    }
    if (symbol_id < snapshot_state.has_last_index_price_by_symbol.size() &&
        snapshot_state.has_last_index_price_by_symbol[symbol_id] != 0 &&
        symbol_id < snapshot_state.last_index_price_by_symbol.size()) {
        out_row.index_price = snapshot_state.last_index_price_by_symbol[symbol_id];
        out_row.has_index_price = true;
        if (symbol_id < snapshot_state.last_index_price_source_by_symbol.size()) {
            out_row.index_price_source = snapshot_state.last_index_price_source_by_symbol[symbol_id];
        }
    }
}

} // namespace

void SnapshotBuilder::Fill(const BinanceExchange& exchange, Contracts::StatusSnapshot& out)
{
    // Read-only snapshot path:
    // - account balances from lightweight account facade state
    // - replay/price context from SnapshotState written by StepKernel
    const auto& runtime_state = *exchange.runtime_state_;
    const auto& snapshot_state = *exchange.snapshot_state_;
    const auto& step_state = *exchange.step_kernel_state_;
    const auto perp_balance = exchange.account_state().get_perp_balance();
    const auto spot_balance = exchange.account_state().get_spot_balance();
    const double total_cash_balance = exchange.account_state().get_total_cash_balance();
    const double perp_available_balance = std::max(
        0.0,
        perp_balance.WalletBalance - perp_balance.PositionInitialMargin - runtime_state.perp_open_order_initial_margin);
    const double spot_available_balance = std::max(
        0.0,
        spot_balance.WalletBalance - spot_balance.PositionInitialMargin - runtime_state.spot_open_order_initial_margin);

    const double base_uncertainty_bps = std::max(0.0, runtime_state.simulation_config.uncertainty_band_bps);
    const double spot_inventory_value = compute_spot_inventory_value(runtime_state, snapshot_state, step_state);
    const double spot_ledger_value = spot_balance.WalletBalance + spot_inventory_value;
    const double total_ledger_value = perp_balance.Equity + spot_ledger_value;
    double mark_index_diag_bps = 0.0;

    out.ts_exchange = snapshot_state.ts_exchange;
    out.wallet_balance = perp_balance.WalletBalance;
    out.margin_balance = perp_balance.MarginBalance;
    out.available_balance = perp_available_balance;
    out.unrealized_pnl = perp_balance.UnrealizedPnl;
    out.total_unrealized_pnl = perp_balance.UnrealizedPnl;
    out.perp_wallet_balance = perp_balance.WalletBalance;
    out.perp_margin_balance = perp_balance.MarginBalance;
    out.perp_available_balance = perp_available_balance;
    out.spot_cash_balance = spot_balance.WalletBalance;
    out.spot_available_balance = spot_available_balance;
    out.spot_inventory_value = spot_inventory_value;
    out.spot_ledger_value = spot_ledger_value;
    out.total_cash_balance = total_cash_balance;
    out.total_ledger_value = total_ledger_value;
    out.total_ledger_value_base = total_ledger_value;
    out.total_ledger_value_conservative = total_ledger_value;
    out.total_ledger_value_optimistic = total_ledger_value;
    out.uncertainty_band_bps = base_uncertainty_bps;
    out.basis_warning_symbols = 0;
    out.basis_stress_symbols = 0;
    out.basis_stress_blocked_orders = runtime_state.basis_stress_blocked_orders_total;
    out.funding_applied_events = exchange.step_kernel_state_->funding_applied_events_total;
    out.funding_skipped_no_mark = exchange.step_kernel_state_->funding_skipped_no_mark_total;
    out.progress_pct = snapshot_state.progress_pct;

    if (!snapshot_state.symbols_shared) {
        out.prices.clear();
        return;
    }
    const size_t symbol_count = snapshot_state.symbols_shared->size();
    const size_t previous_price_row_count = out.prices.size();
    out.prices.resize(symbol_count);
    const double basis_warning_bps = std::max(0.0, runtime_state.simulation_config.basis_warning_bps);
    const double basis_stress_bps = std::max(0.0, runtime_state.simulation_config.basis_stress_bps);
    const bool overlay_enabled = runtime_state.simulation_config.simulator_risk_overlay_enabled;
    const auto& symbols = *snapshot_state.symbols_shared;
    const bool is_runtime_cache_target = &out == &runtime_state.last_status_snapshot;
    bool has_symbol_alignment = previous_price_row_count == symbol_count;
    if (has_symbol_alignment) {
        for (size_t i = 0; i < symbol_count; ++i) {
            if (out.prices[i].symbol != symbols[i]) {
                has_symbol_alignment = false;
                break;
            }
        }
    }
    const bool can_incremental_materialize =
        is_runtime_cache_target &&
        has_symbol_alignment &&
        snapshot_state.price_rows_version > 0 &&
        snapshot_state.price_rows_by_symbol.size() == symbol_count &&
        !snapshot_state.dirty_price_symbol_ids.empty();
    const bool can_skip_row_materialization =
        is_runtime_cache_target &&
        has_symbol_alignment &&
        snapshot_state.price_rows_version > 0 &&
        snapshot_state.price_rows_by_symbol.size() == symbol_count &&
        snapshot_state.dirty_price_symbol_ids.empty();
    if (can_incremental_materialize) {
        for (const auto dirty_symbol_id : snapshot_state.dirty_price_symbol_ids) {
            if (dirty_symbol_id >= symbol_count) {
                continue;
            }
            materialize_price_row(snapshot_state, symbols, dirty_symbol_id, out.prices[dirty_symbol_id]);
        }
    }
    else if (!can_skip_row_materialization) {
        for (size_t i = 0; i < symbol_count; ++i) {
            materialize_price_row(snapshot_state, symbols, i, out.prices[i]);
        }
    }

    for (size_t i = 0; i < symbol_count; ++i) {
        auto& price = out.prices[i];
        const bool has_mark_ts = i < snapshot_state.last_mark_price_ts_by_symbol.size() &&
            snapshot_state.last_mark_price_ts_by_symbol[i] != kUnsetSnapshotTimestamp;
        const bool has_index_ts = i < snapshot_state.last_index_price_ts_by_symbol.size() &&
            snapshot_state.last_index_price_ts_by_symbol[i] != kUnsetSnapshotTimestamp;
        const bool mark_index_ts_coherent = has_mark_ts &&
            has_index_ts &&
            snapshot_state.last_mark_price_ts_by_symbol[i] == snapshot_state.last_index_price_ts_by_symbol[i] &&
            snapshot_state.last_mark_price_ts_by_symbol[i] == snapshot_state.ts_exchange;

        if (basis_warning_bps > 0.0 &&
            price.has_mark_price &&
            price.has_index_price &&
            mark_index_ts_coherent &&
            std::abs(price.index_price) > 1e-12) {
            const double basis_bps = std::abs((price.mark_price - price.index_price) / price.index_price) * 10000.0;
            mark_index_diag_bps = std::max(mark_index_diag_bps, basis_bps);
            if (overlay_enabled) {
                if (basis_bps >= basis_warning_bps) {
                    ++out.basis_warning_symbols;
                }
                if (basis_stress_bps > 0.0 && basis_bps >= basis_stress_bps) {
                    ++out.basis_stress_symbols;
                }
            }
        }
    }
    if (out.basis_stress_symbols > 0) {
        out.basis_warning_symbols = out.basis_stress_symbols;
    }
    out.uncertainty_band_bps = base_uncertainty_bps + mark_index_diag_bps;
    const double band_ratio = out.uncertainty_band_bps / 10000.0;
    out.total_ledger_value_conservative = out.total_ledger_value_base * (1.0 - band_ratio);
    out.total_ledger_value_optimistic = out.total_ledger_value_base * (1.0 + band_ratio);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
