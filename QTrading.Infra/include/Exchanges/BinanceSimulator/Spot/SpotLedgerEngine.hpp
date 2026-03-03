#pragma once

class SpotLedgerEngine {
public:
    explicit SpotLedgerEngine(double initial_cash = 0.0);

    double cash_balance() const noexcept;
    void set_cash_balance(double value) noexcept;

    void credit_cash(double amount) noexcept;
    void debit_cash(double amount) noexcept;

private:
    double cash_balance_{ 0.0 };
};
