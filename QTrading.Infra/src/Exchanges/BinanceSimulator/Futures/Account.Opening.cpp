#include "Exchanges/BinanceSimulator/Futures/Account.hpp"

// This method is declared on Account but must be defined out-of-line.

void Account::processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
    double fee, double feeRate, std::vector<Order>& leftover)
{
    if (ord.reduce_only) {
        if (!processReduceOnlyOrder(ord, fill_qty, fill_price, fee, leftover)) {
            // If no matching position to reduce, ignore the order.
        }
    }
    else {
        processNormalOpeningOrder(ord, fill_qty, fill_price, notional, fee, feeRate, leftover);
    }
}
