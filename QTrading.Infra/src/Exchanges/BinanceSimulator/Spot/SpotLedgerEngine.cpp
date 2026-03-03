#include "Exchanges/BinanceSimulator/Spot/SpotLedgerEngine.hpp"

SpotLedgerEngine::SpotLedgerEngine(double initial_cash)
    : cash_balance_(initial_cash)
{
}

double SpotLedgerEngine::cash_balance() const noexcept
{
    return cash_balance_;
}

void SpotLedgerEngine::set_cash_balance(double value) noexcept
{
    cash_balance_ = value;
}

void SpotLedgerEngine::credit_cash(double amount) noexcept
{
    cash_balance_ += amount;
}

void SpotLedgerEngine::debit_cash(double amount) noexcept
{
    cash_balance_ -= amount;
}
