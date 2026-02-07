#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <iostream>

using QTrading::Dto::Trading::OrderSide;

void Account::applySpotClosingCashflow(double close_qty, double fill_price, double fee, double& freed_margin, double& freed_maint)
{
    const double proceeds = fill_price * close_qty;
    spot_ledger_.credit_cash(proceeds);
    spot_ledger_.debit_cash(fee);
    freed_margin = 0.0;
    freed_maint = 0.0;
}

bool Account::processSpotOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover)
{
    const auto& instrument_spec = resolve_instrument_spec_(ord.symbol);
    if (!instrument_spec.allow_short && ord.side != OrderSide::Buy) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Spot open order must be BUY. id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }
    if (hedge_mode_) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Spot symbol does not support hedge mode. id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }

    const double required_cash = notional + fee;
    if (spot_ledger_.cash_balance() + 1e-12 < required_cash) {
        if (enable_console_output_) {
            std::cerr << "[processNormalOpeningOrder] Not enough spot cash for order id=" << ord.id << "\n";
        }
        leftover.push_back(ord);
        return false;
    }

    const double lev = 1.0;
    const double init_margin = notional;
    const double maint_margin = 0.0;
    spot_ledger_.debit_cash(required_cash);
    applyOpeningFillToPosition(ord, fill_qty, fill_price, notional, init_margin, maint_margin, fee, lev, feeRate, true);
    mark_balance_dirty_();

    const double leftover_qty = ord.quantity - fill_qty;
    if (leftover_qty > 1e-8) {
        ord.quantity = leftover_qty;
        leftover.push_back(ord);
    }
    return true;
}
