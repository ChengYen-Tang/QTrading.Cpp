#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

AccountPolicies AccountPolicies::Default()
{
    AccountPolicies policies{};
    policies.fee_rates = [](int vip_level) {
        auto it = ::vip_fee_rates.find(vip_level);
        if (it == ::vip_fee_rates.end()) {
            it = ::vip_fee_rates.find(0);
        }
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    };
    return policies;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
