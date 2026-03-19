#pragma once

#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

class V2ReplayScenarioPack final {
public:
    static std::vector<ReplayCompareScenarioData> BuildCoreScenarioPack();
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
