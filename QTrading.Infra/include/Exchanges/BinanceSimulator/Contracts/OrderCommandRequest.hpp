#pragma once

#include <cstdint>
#include <string>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// Enumerates normalized order commands accepted by the simulator kernels.
enum class OrderCommandKind : uint8_t {
    SpotLimit = 0,
    SpotMarket = 1,
    SpotMarketQuote = 2,
    PerpLimit = 3,
    PerpMarket = 4,
    PerpClosePosition = 5,
};

/// Normalized order-command payload produced by facade APIs before kernel handling.
struct OrderCommandRequest {
    /// Requested command lane and execution style.
    OrderCommandKind kind{ OrderCommandKind::PerpMarket };
    /// Instrument lane resolved for the symbol.
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    /// Target symbol.
    std::string symbol;
    /// Base-asset quantity for spot/perp commands.
    double quantity{ 0.0 };
    /// Limit price, or zero for market-style commands.
    double price{ 0.0 };
    /// Quote-order quantity for quote-sized spot market orders.
    double quote_order_qty{ 0.0 };
    /// Requested buy/sell side.
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    /// Requested hedge-mode position side.
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    /// Limit-order time in force; defaults to GTC for backward compatibility.
    QTrading::Dto::Trading::TimeInForce time_in_force{ QTrading::Dto::Trading::TimeInForce::GTC };
    /// True when the command must not increase exposure.
    bool reduce_only{ false };
    /// True when the command represents close-position semantics.
    bool close_position{ false };
    /// Optional client-provided order identifier.
    std::string client_order_id;
    /// Encoded self-trade-prevention mode.
    int stp_mode{ 0 };
    /// First replay step when matching is allowed for this request.
    uint64_t first_matching_step{ 0 };
};

/// Scheduler-owned deferred command record for async order latency simulation.
struct DeferredOrderCommand {
    /// Async request identifier assigned on submission.
    uint64_t request_id{ 0 };
    /// Step when the command was submitted.
    uint64_t submitted_step{ 0 };
    /// Step when the command becomes executable.
    uint64_t due_step{ 0 };
    /// Original normalized command payload.
    OrderCommandRequest request{};
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
