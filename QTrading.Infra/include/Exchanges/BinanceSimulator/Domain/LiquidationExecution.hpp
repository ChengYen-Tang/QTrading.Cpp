#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Dto/Trading/InstrumentSpec.hpp"

namespace QTrading::Dto::Market::Binance {
struct MultiKlineDto;
}

namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Compact delta record for liquidation-induced position reductions.
struct LiquidationPositionDelta {
    int64_t position_id{ 0 };
    std::string symbol;
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    bool is_long{ true };
    double entry_price{ 0.0 };
    double quantity_before{ 0.0 };
    double quantity_closed{ 0.0 };
    bool position_closed{ false };
};

/// Executes the reduced liquidation path once eligibility has been established.
class LiquidationExecution final {
public:
    /// Cancels conflicting orders and reduces the worst distressed perp position.
    static bool Run(
        State::BinanceExchangeRuntimeState& runtime_state,
        Account& account,
        State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload,
        std::vector<LiquidationPositionDelta>* out_position_deltas = nullptr) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
