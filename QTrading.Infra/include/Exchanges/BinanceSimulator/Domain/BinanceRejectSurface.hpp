#pragma once

#include <optional>
#include <string>
#include <utility>

#include "Exchanges/BinanceSimulator/Contracts/OrderRejectInfo.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class BinanceRejectSurface final {
public:
    static std::pair<int, std::string> MapToBinanceError(
        const std::optional<Contracts::OrderRejectInfo>& reject);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
