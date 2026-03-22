#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

#include <stdexcept>

namespace QTrading::Infra::Exchanges::BinanceSim::Support {

Account::AccountInitConfig BuildInitConfig(double init_balance, int vip_level)
{
    Account::AccountInitConfig cfg{};
    cfg.init_balance = init_balance;
    cfg.spot_initial_cash = init_balance;
    cfg.perp_initial_wallet = 0.0;
    cfg.vip_level = vip_level;
    return cfg;
}

[[noreturn]] void ThrowNotImplemented(const char* function_name)
{
    throw std::runtime_error(std::string(function_name) + " not implemented in rebuilt BinanceExchange facade");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Support
