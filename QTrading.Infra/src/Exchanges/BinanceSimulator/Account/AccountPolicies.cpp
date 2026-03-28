#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

AccountPolicies AccountPolicies::Default()
{
    AccountPolicies policies{};
    policies.fee_rates = [](int) {
        return std::make_tuple(0.0002, 0.0004);
    };
    return policies;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
