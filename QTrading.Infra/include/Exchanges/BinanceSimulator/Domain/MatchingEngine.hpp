#pragma once

#include <string>
#include <vector>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Dto::Market::Binance {
struct MultiKlineDto;
}

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct MatchFill {
    int order_id{ 0 };
    std::string symbol;
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    bool reduce_only{ false };
    bool close_position{ false };
    bool is_taker{ false };
    double quantity{ 0.0 };
    double price{ 0.0 };
};

/// Minimal matching engine for the current restored execution path.
/// Supports market/limit matching and partial/no-fill based on bar liquidity.
class MatchingEngine final {
public:
    static void RunStep(State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market,
        std::vector<MatchFill>& out_fills);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
