#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <iostream>

using QTrading::Dto::Trading::PositionSide;

void Account::applyPerpClosingCashflow(double realized_pnl, double fee, double freed_margin)
{
    perp_ledger_.credit_wallet(realized_pnl);
    perp_ledger_.debit_wallet(fee);
    perp_ledger_.decrease_used_margin(freed_margin);
}

bool Account::processPerpOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover)
{
    const auto& instrument_spec = resolve_instrument_spec_(ord.symbol);
    const double lev = get_symbol_leverage(ord.symbol);

    double max_lev = 1.0;
    std::tie(std::ignore, max_lev) = get_tier_info(notional);

    if (instrument_spec.max_leverage > 0.0 && lev > instrument_spec.max_leverage) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Instrument leverage cap exceeded for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }
    if (lev > max_lev) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Leverage too high for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }

    const double init_margin = notional / lev;
    const double maint_margin = maintenance_margin_for_notional_(notional);
    const auto snap = get_perp_balance();
    const double required = init_margin + fee;
    if (snap.AvailableBalance < required) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Not enough available balance for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }

    if (hedge_mode_ && ord.position_side == PositionSide::Both) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Hedge-mode order must specify position_side (Long/Short). id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }

    perp_ledger_.debit_wallet(fee);
    perp_ledger_.increase_used_margin(init_margin);
    applyOpeningFillToPosition(ord, fill_qty, fill_price, notional, init_margin, maint_margin, fee, lev, feeRate, false);
    mark_balance_dirty_();

    const double leftover_qty = ord.quantity - fill_qty;
    if (leftover_qty > 1e-8) {
        ord.quantity = leftover_qty;
        leftover.push_back(ord);
    }
    return true;
}
