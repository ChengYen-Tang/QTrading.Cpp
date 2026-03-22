#pragma once

#include "Dto/Account/BalanceSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

class AccountApi {
public:
    explicit AccountApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const;
    QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const;
    double get_total_cash_balance() const;
    bool transfer_spot_to_perp(double amount);
    bool transfer_perp_to_spot(double amount);

private:
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
