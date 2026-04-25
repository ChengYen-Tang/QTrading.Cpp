#pragma once

#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

class V2ReplayScenarioPack {
public:
    static std::vector<ReplayCompareScenarioData> BuildCoreScenarioPack()
    {
        auto make = [](const char* name) {
            ReplayCompareScenarioData data{};
            data.scenario.name = name;
            data.legacy_steps.resize(64);
            data.candidate_steps.resize(64);
            return data;
        };

        return {
            make("v2-vs-legacy.basis-stress"),
            make("v2-vs-legacy.mixed-spot-perp"),
            make("v2-vs-legacy.funding-reference-edge"),
            make("v2-vs-legacy.async-ack-latency"),
        };
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare

