#include "Exchanges/BinanceSimulator/Output/SnapshotBuilder.hpp"

#include <algorithm>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {
namespace {

double compute_spot_inventory_value(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::SnapshotState& snapshot_state)
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
        for (size_t i = 0; i < snapshot_state.symbols_shared->size(); ++i) {
            if ((*snapshot_state.symbols_shared)[i] != position.symbol) {
                continue;
            }
            if (i < snapshot_state.has_last_trade_price_by_symbol.size() &&
                i < snapshot_state.last_trade_price_by_symbol.size() &&
                snapshot_state.has_last_trade_price_by_symbol[i] != 0) {
                price = snapshot_state.last_trade_price_by_symbol[i];
            }
            break;
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
    const auto perp_balance = exchange.account_state().get_perp_balance();
    const auto spot_balance = exchange.account_state().get_spot_balance();
    const double total_cash_balance = exchange.account_state().get_total_cash_balance();

    const double uncertainty_bps = std::max(0.0, runtime_state.simulation_config.uncertainty_band_bps);
    const double spot_inventory_value = compute_spot_inventory_value(runtime_state, snapshot_state);
    const double spot_ledger_value = spot_balance.WalletBalance + spot_inventory_value;
    const double total_ledger_value = perp_balance.Equity + spot_ledger_value;
    const double band_ratio = uncertainty_bps / 10000.0;

    out.ts_exchange = snapshot_state.ts_exchange;
    out.wallet_balance = perp_balance.WalletBalance;
    out.margin_balance = perp_balance.MarginBalance;
    out.available_balance = perp_balance.AvailableBalance;
    out.unrealized_pnl = perp_balance.UnrealizedPnl;
    out.total_unrealized_pnl = perp_balance.UnrealizedPnl;
    out.perp_wallet_balance = perp_balance.WalletBalance;
    out.perp_margin_balance = perp_balance.MarginBalance;
    out.perp_available_balance = perp_balance.AvailableBalance;
    out.spot_cash_balance = spot_balance.WalletBalance;
    out.spot_available_balance = spot_balance.AvailableBalance;
    out.spot_inventory_value = spot_inventory_value;
    out.spot_ledger_value = spot_ledger_value;
    out.total_cash_balance = total_cash_balance;
    out.total_ledger_value = total_ledger_value;
    out.total_ledger_value_base = total_ledger_value;
    out.total_ledger_value_conservative = total_ledger_value * (1.0 - band_ratio);
    out.total_ledger_value_optimistic = total_ledger_value * (1.0 + band_ratio);
    out.uncertainty_band_bps = uncertainty_bps;
    out.basis_warning_symbols = 0;
    out.basis_stress_symbols = 0;
    out.basis_stress_blocked_orders = 0;
    out.funding_applied_events = 0;
    out.funding_skipped_no_mark = 0;
    out.progress_pct = snapshot_state.progress_pct;

    out.prices.clear();
    if (!snapshot_state.symbols_shared) {
        return;
    }
    out.prices.reserve(snapshot_state.symbols_shared->size());
    for (size_t i = 0; i < snapshot_state.symbols_shared->size(); ++i) {
        Contracts::StatusPriceSnapshot price{};
        price.symbol = (*snapshot_state.symbols_shared)[i];
        if (i < snapshot_state.has_last_trade_price_by_symbol.size() &&
            snapshot_state.has_last_trade_price_by_symbol[i] != 0 &&
            i < snapshot_state.last_trade_price_by_symbol.size()) {
            price.trade_price = snapshot_state.last_trade_price_by_symbol[i];
            price.has_trade_price = true;
            price.price = price.trade_price;
            price.has_price = true;
        }
        out.prices.emplace_back(std::move(price));
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
