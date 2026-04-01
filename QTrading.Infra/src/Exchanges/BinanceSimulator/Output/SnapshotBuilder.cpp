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
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot || !position.is_long) {
            continue;
        }
        double price = position.entry_price;
        const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
        if (symbol_it != step_state.symbol_to_id.end()) {
            const size_t i = symbol_it->second;
            if (i < snapshot_state.has_last_trade_price_by_symbol.size() &&
                i < snapshot_state.last_trade_price_by_symbol.size() &&
                snapshot_state.has_last_trade_price_by_symbol[i] != 0) {
                price = snapshot_state.last_trade_price_by_symbol[i];
            }
        }
        total += position.quantity * price;
    }
    return total;
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
    out.prices.resize(symbol_count);
    const double basis_warning_bps = std::max(0.0, runtime_state.simulation_config.basis_warning_bps);
    const double basis_stress_bps = std::max(0.0, runtime_state.simulation_config.basis_stress_bps);
    const bool overlay_enabled = runtime_state.simulation_config.simulator_risk_overlay_enabled;
    const auto& symbols = *snapshot_state.symbols_shared;
    for (size_t i = 0; i < symbol_count; ++i) {
        auto& price = out.prices[i];
        if (price.symbol != symbols[i]) {
            price.symbol = symbols[i];
        }
        price.price = 0.0;
        price.has_price = false;
        price.trade_price = 0.0;
        price.has_trade_price = false;
        price.mark_price = 0.0;
        price.has_mark_price = false;
        price.mark_price_source = kReferencePriceSourceNone;
        price.index_price = 0.0;
        price.has_index_price = false;
        price.index_price_source = kReferencePriceSourceNone;
        if (i < snapshot_state.has_last_trade_price_by_symbol.size() &&
            snapshot_state.has_last_trade_price_by_symbol[i] != 0 &&
            i < snapshot_state.last_trade_price_by_symbol.size()) {
            price.trade_price = snapshot_state.last_trade_price_by_symbol[i];
            price.has_trade_price = true;
            price.price = price.trade_price;
            price.has_price = true;
        }
        if (i < snapshot_state.has_last_mark_price_by_symbol.size() &&
            snapshot_state.has_last_mark_price_by_symbol[i] != 0 &&
            i < snapshot_state.last_mark_price_by_symbol.size()) {
            price.mark_price = snapshot_state.last_mark_price_by_symbol[i];
            price.has_mark_price = true;
            if (i < snapshot_state.last_mark_price_source_by_symbol.size()) {
                price.mark_price_source = snapshot_state.last_mark_price_source_by_symbol[i];
            }
        }
        if (i < snapshot_state.has_last_index_price_by_symbol.size() &&
            snapshot_state.has_last_index_price_by_symbol[i] != 0 &&
            i < snapshot_state.last_index_price_by_symbol.size()) {
            price.index_price = snapshot_state.last_index_price_by_symbol[i];
            price.has_index_price = true;
            if (i < snapshot_state.last_index_price_source_by_symbol.size()) {
                price.index_price_source = snapshot_state.last_index_price_source_by_symbol[i];
            }
        }
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
