#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct SymbolDataset {
    std::string symbol;
    std::string kline_csv;
    std::optional<std::string> funding_csv;
    std::optional<std::string> mark_kline_csv;
    std::optional<std::string> index_kline_csv;
    std::optional<QTrading::Dto::Trading::InstrumentType> instrument_type;
};

struct AsyncOrderAck {
    enum class Status {
        Pending = 0,
        Accepted = 1,
        Rejected = 2,
    };

    uint64_t request_id{ 0 };
    Status status{ Status::Pending };
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    std::string symbol;
    double quantity{ 0.0 };
    double price{ 0.0 };
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    bool reduce_only{ false };
    uint64_t submitted_step{ 0 };
    uint64_t due_step{ 0 };
    uint64_t resolved_step{ 0 };
    int reject_code{ 0 };
    std::string reject_message;
    std::string client_order_id;
    int stp_mode{ 0 };
    int binance_error_code{ 0 };
    std::string binance_error_message;
    bool close_position{ false };
};

enum class FundingApplyTiming : uint8_t {
    BeforeMatching = 0,
    AfterMatching = 1,
};

enum class ReferencePriceSource : int32_t {
    None = 0,
    Raw = 1,
    Interpolated = 2,
};

struct ReferenceFundingResolverDiagnostic {
    bool applied{ false };
    bool has_mark_price{ false };
    uint64_t funding_time{ 0 };
    double funding_rate{ 0.0 };
    ReferencePriceSource mark_price_source{ ReferencePriceSource::None };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
