#include "Exchanges/BinanceSimulator/Output/StatusSnapshotBuilder.hpp"

#include <algorithm>

namespace QTrading::Infra::Exchanges::BinanceSim {

namespace {

double spot_inventory_value_from_positions(const std::vector<dto::Position>& positions)
{
    double value = 0.0;
    for (const auto& p : positions) {
        if (p.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot) {
            continue;
        }
        if (p.quantity <= 0.0) {
            continue;
        }
        value += (p.entry_price * p.quantity + p.unrealized_pnl);
    }
    return value;
}

} // namespace

void BinanceExchange::StatusSnapshotBuilder::Fill(const BinanceExchange& owner,
    BinanceExchange::StatusSnapshot& out)
{
    double uncertainty_bps = 0.0;
    double mark_index_diag_bps = 0.0;
    {
        std::lock_guard<std::mutex> lk(owner.account_mtx_);
        uncertainty_bps = owner.uncertainty_band_bps_;
        if (owner.account_engine_) {
            const auto perp_bal = owner.account_engine_->get_perp_balance();
            const auto spot_bal = owner.account_engine_->get_spot_balance();
            const auto positions = owner.account_engine_->get_all_positions();
            const double spot_inventory_value = spot_inventory_value_from_positions(positions);
            const double spot_ledger_value = spot_bal.WalletBalance + spot_inventory_value;
            const double total_cash_balance = owner.account_engine_->get_total_cash_balance();
            const double total_ledger_value = perp_bal.Equity + spot_ledger_value;

            out.wallet_balance = perp_bal.WalletBalance;
            out.margin_balance = perp_bal.MarginBalance;
            out.available_balance = perp_bal.AvailableBalance;
            out.unrealized_pnl = perp_bal.UnrealizedPnl;
            out.total_unrealized_pnl = owner.account_engine_->total_unrealized_pnl();
            out.perp_wallet_balance = perp_bal.WalletBalance;
            out.perp_margin_balance = perp_bal.MarginBalance;
            out.perp_available_balance = perp_bal.AvailableBalance;
            out.spot_cash_balance = spot_bal.WalletBalance;
            out.spot_available_balance = spot_bal.AvailableBalance;
            out.spot_inventory_value = spot_inventory_value;
            out.spot_ledger_value = spot_ledger_value;
            out.total_cash_balance = total_cash_balance;
            out.total_ledger_value = total_ledger_value;
            out.total_ledger_value_base = total_ledger_value;
            const double band = std::max(0.0, uncertainty_bps) / 10000.0;
            out.total_ledger_value_conservative = total_ledger_value * (1.0 - band);
            out.total_ledger_value_optimistic = total_ledger_value * (1.0 + band);
        }
        else {
            out.wallet_balance = 0.0;
            out.margin_balance = 0.0;
            out.available_balance = 0.0;
            out.unrealized_pnl = 0.0;
            out.total_unrealized_pnl = 0.0;
            out.perp_wallet_balance = 0.0;
            out.perp_margin_balance = 0.0;
            out.perp_available_balance = 0.0;
            out.spot_cash_balance = 0.0;
            out.spot_available_balance = 0.0;
            out.spot_inventory_value = 0.0;
            out.spot_ledger_value = 0.0;
            out.total_cash_balance = 0.0;
            out.total_ledger_value = 0.0;
            out.total_ledger_value_base = 0.0;
            out.total_ledger_value_conservative = 0.0;
            out.total_ledger_value_optimistic = 0.0;
        }
    }

    {
        std::lock_guard<std::mutex> state_lk(owner.state_mtx_);
        mark_index_diag_bps = std::max(0.0, owner.last_mark_index_max_abs_basis_bps_);
        out.ts_exchange = owner.last_step_ts_;
        out.progress_pct = owner.progress_pct_unlocked_();
        out.basis_warning_symbols = owner.simulator_risk_overlay_.warning_symbols;
        out.basis_stress_symbols = owner.simulator_risk_overlay_.stress_symbols;
        out.prices.clear();
        out.prices.reserve(owner.symbols_.size());
        for (size_t i = 0; i < owner.symbols_.size(); ++i) {
            StatusSnapshot::PriceSnapshot snap;
            snap.symbol = owner.symbols_[i];
            snap.trade_price = owner.last_close_by_symbol_[i];
            snap.has_trade_price = owner.has_last_close_[i] != 0;
            snap.price = snap.trade_price;
            snap.has_price = snap.has_trade_price;
            snap.mark_price = (i < owner.last_mark_by_symbol_.size()) ? owner.last_mark_by_symbol_[i] : 0.0;
            snap.has_mark_price = (i < owner.has_last_mark_.size()) && (owner.has_last_mark_[i] != 0);
            snap.mark_price_source = (i < owner.last_mark_source_by_symbol_.size())
                ? owner.last_mark_source_by_symbol_[i]
                : static_cast<int32_t>(ReferencePriceSource::None);
            snap.index_price = (i < owner.last_index_by_symbol_.size()) ? owner.last_index_by_symbol_[i] : 0.0;
            snap.has_index_price = (i < owner.has_last_index_.size()) && (owner.has_last_index_[i] != 0);
            snap.index_price_source = (i < owner.last_index_source_by_symbol_.size())
                ? owner.last_index_source_by_symbol_[i]
                : static_cast<int32_t>(ReferencePriceSource::None);
            out.prices.emplace_back(std::move(snap));
        }
        out.funding_applied_events = owner.funding_applied_events_total_;
        out.funding_skipped_no_mark = owner.funding_skipped_no_mark_total_;
    }
    out.basis_stress_blocked_orders = owner.simulator_risk_overlay_.stress_blocked_orders.load(std::memory_order_relaxed);
    out.uncertainty_band_bps = std::max(0.0, uncertainty_bps) + std::max(0.0, mark_index_diag_bps);
    const double total_band = out.uncertainty_band_bps / 10000.0;
    out.total_ledger_value_conservative = out.total_ledger_value_base * (1.0 - total_band);
    out.total_ledger_value_optimistic = out.total_ledger_value_base * (1.0 + total_band);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
