#pragma once

#include <string>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Support {

/// Builds a legacy-compatible init config from simple balance + vip inputs.
Account::AccountInitConfig BuildInitConfig(double init_balance, int vip_level);

} // namespace QTrading::Infra::Exchanges::BinanceSim::Support
