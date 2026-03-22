#pragma once

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

class AccountCoreV2 {
public:
    AccountCoreV2() = default;
    explicit AccountCoreV2(const Account::AccountInitConfig& init)
        : init_(init) {}

private:
    Account::AccountInitConfig init_{};
};

} // namespace QTrading::Infra::Exchanges::BinanceSim
