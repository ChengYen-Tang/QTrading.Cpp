#pragma once

#include <cstddef>
#include <limits>
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

/// One fill fragment generated while matching the current open-order book.
struct MatchFill {
    /// Internal order id that produced the fill.
    int order_id{ 0 };
    /// Runtime symbol id aligned to StepKernel symbol table when available.
    size_t symbol_id{ std::numeric_limits<size_t>::max() };
    /// Symbol being matched.
    std::string symbol;
    /// Instrument lane for the fill.
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    /// Executed side of the originating order.
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    /// Position side used for hedge-mode fills.
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    /// True when the originating order was reduce-only.
    bool reduce_only{ false };
    /// True when the originating order was close-position style.
    bool close_position{ false };
    /// True when the fill consumed liquidity immediately.
    bool is_taker{ false };
    /// Final fill probability applied to the order.
    double fill_probability{ 1.0 };
    /// Probability of classifying the order as taker in probabilistic models.
    double taker_probability{ 0.0 };
    /// Market-impact slippage applied to the fill in basis points.
    double impact_slippage_bps{ 0.0 };
    /// Quote quantity carried by quote-sized spot market orders.
    double quote_order_qty{ 0.0 };
    /// Original order price before fill-price adjustments.
    double order_price{ 0.0 };
    /// Position id being closed when close/reduce logic targets a specific leg.
    int64_t closing_position_id{ 0 };
    /// Original requested quantity.
    double order_quantity{ 0.0 };
    /// Executed fill quantity.
    double quantity{ 0.0 };
    /// Executed fill price.
    double price{ 0.0 };
};

/// Minimal matching engine for the current restored execution path.
/// Supports market/limit matching and partial/no-fill based on bar liquidity.
class MatchingEngine final {
public:
    /// Matches currently open orders against the current replay market payload.
    static void RunStep(State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        const QTrading::Dto::Market::Binance::MultiKlineDto& market,
        std::vector<MatchFill>& out_fills);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
