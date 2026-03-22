#pragma once

#include <optional>
#include <string>
#include <utility>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class BinanceRejectSurface final {
public:
    static std::pair<int, std::string> MapToBinanceError(
        const std::optional<Account::OrderRejectInfo>& reject);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
