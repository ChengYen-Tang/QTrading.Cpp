#pragma once

#include <vector>

#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Minimal settlement engine for the current matching and fill path.
/// Applies fill deltas to account balances and spot/perp positions.
class FillSettlementEngine final {
public:
    static void Apply(State::BinanceExchangeRuntimeState& runtime_state, Account& account,
        const std::vector<MatchFill>& fills);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
