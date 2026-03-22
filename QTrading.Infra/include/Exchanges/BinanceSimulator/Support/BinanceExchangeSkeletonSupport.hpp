#pragma once

#include <string>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Support {

Account::AccountInitConfig BuildInitConfig(double init_balance, int vip_level);
[[noreturn]] void ThrowNotImplemented(const char* function_name);

} // namespace QTrading::Infra::Exchanges::BinanceSim::Support
