#pragma once

#include <cstdint>
#include <string>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

enum class OrderCommandKind : uint8_t {
    SpotLimit = 0,
    SpotMarket = 1,
    SpotMarketQuote = 2,
    PerpLimit = 3,
    PerpMarket = 4,
    PerpClosePosition = 5,
};

struct OrderCommandRequest {
    OrderCommandKind kind{ OrderCommandKind::PerpMarket };
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    std::string symbol;
    double quantity{ 0.0 };
    double price{ 0.0 };
    double quote_order_qty{ 0.0 };
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    bool reduce_only{ false };
    bool close_position{ false };
    std::string client_order_id;
    int stp_mode{ 0 };
};

struct DeferredOrderCommand {
    uint64_t request_id{ 0 };
    uint64_t submitted_step{ 0 };
    uint64_t due_step{ 0 };
    OrderCommandRequest request{};
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
