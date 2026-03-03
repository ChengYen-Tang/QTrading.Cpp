#include "Exchanges/BinanceSimulator/Perp/PerpLedgerEngine.hpp"

#include <algorithm>

PerpLedgerEngine::PerpLedgerEngine(double initial_wallet)
    : wallet_balance_(initial_wallet)
{
}

double PerpLedgerEngine::wallet_balance() const noexcept
{
    return wallet_balance_;
}

void PerpLedgerEngine::set_wallet_balance(double value) noexcept
{
    wallet_balance_ = value;
}

double PerpLedgerEngine::used_margin() const noexcept
{
    return used_margin_;
}

void PerpLedgerEngine::set_used_margin(double value) noexcept
{
    used_margin_ = value;
}

void PerpLedgerEngine::credit_wallet(double amount) noexcept
{
    wallet_balance_ += amount;
}

void PerpLedgerEngine::debit_wallet(double amount) noexcept
{
    wallet_balance_ -= amount;
}

void PerpLedgerEngine::increase_used_margin(double amount) noexcept
{
    used_margin_ += amount;
}

void PerpLedgerEngine::decrease_used_margin(double amount) noexcept
{
    used_margin_ = std::max(0.0, used_margin_ - amount);
}

void PerpLedgerEngine::reset_bankruptcy() noexcept
{
    wallet_balance_ = 0.0;
    used_margin_ = 0.0;
}
