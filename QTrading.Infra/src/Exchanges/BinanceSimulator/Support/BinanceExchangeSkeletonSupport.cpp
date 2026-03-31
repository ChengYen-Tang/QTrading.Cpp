#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Support {

Account::AccountInitConfig BuildInitConfig(double init_balance, int vip_level)
{
    // Keeps old constructor-style test input usable with AccountInitConfig API.
    Account::AccountInitConfig cfg{};
    cfg.init_balance = init_balance;
    cfg.spot_initial_cash = init_balance;
    cfg.perp_initial_wallet = init_balance;
    cfg.vip_level = vip_level;
    return cfg;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Support
