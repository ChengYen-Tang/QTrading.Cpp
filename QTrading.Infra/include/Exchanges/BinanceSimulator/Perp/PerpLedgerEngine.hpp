#pragma once

class PerpLedgerEngine {
public:
    explicit PerpLedgerEngine(double initial_wallet = 0.0);

    double wallet_balance() const noexcept;
    void set_wallet_balance(double value) noexcept;

    double used_margin() const noexcept;
    void set_used_margin(double value) noexcept;

    void credit_wallet(double amount) noexcept;
    void debit_wallet(double amount) noexcept;

    void increase_used_margin(double amount) noexcept;
    void decrease_used_margin(double amount) noexcept;

    void reset_bankruptcy() noexcept;

private:
    double wallet_balance_{ 0.0 };
    double used_margin_{ 0.0 };
};
