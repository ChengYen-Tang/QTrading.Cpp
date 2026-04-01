#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// File-backed replay inputs for one simulator symbol.
struct SymbolDataset {
    /// Canonical symbol exposed by the facade and runtime state.
    std::string symbol;
    /// Required trade-kline CSV used by the replay timeline.
    std::string kline_csv;
    /// Optional funding-rate CSV for perp funding application.
    std::optional<std::string> funding_csv;
    /// Optional mark-price CSV used by reference-price and liquidation logic.
    std::optional<std::string> mark_kline_csv;
    /// Optional index-price CSV used by basis/reference diagnostics.
    std::optional<std::string> index_kline_csv;
    /// Optional explicit instrument typing when symbol suffixes are insufficient.
    std::optional<QTrading::Dto::Trading::InstrumentType> instrument_type;
};

/// Deferred async-order acknowledgement payload stored until its due step resolves.
struct AsyncOrderAck {
    /// Lifecycle state for the deferred acknowledgement record.
    enum class Status {
        Pending = 0,
        Accepted = 1,
        Rejected = 2,
    };

    /// Monotonic request identifier assigned by the simulator.
    uint64_t request_id{ 0 };
    /// Current acknowledgement state.
    Status status{ Status::Pending };
    /// Instrument lane the command belongs to.
    QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
    /// Symbol targeted by the deferred command.
    std::string symbol;
    /// Requested order quantity.
    double quantity{ 0.0 };
    /// Requested limit price, or zero for market commands.
    double price{ 0.0 };
    /// Requested trade side.
    QTrading::Dto::Trading::OrderSide side{ QTrading::Dto::Trading::OrderSide::Buy };
    /// Requested position side in hedge mode.
    QTrading::Dto::Trading::PositionSide position_side{ QTrading::Dto::Trading::PositionSide::Both };
    /// True when the command is allowed to reduce exposure only.
    bool reduce_only{ false };
    /// Step when the request entered the async scheduler.
    uint64_t submitted_step{ 0 };
    /// Step when the request becomes eligible to resolve.
    uint64_t due_step{ 0 };
    /// Step when the request actually resolved.
    uint64_t resolved_step{ 0 };
    /// Internal simulator reject code for rejected commands.
    int reject_code{ 0 };
    /// Human-readable simulator reject reason.
    std::string reject_message;
    /// Optional client-supplied order identifier.
    std::string client_order_id;
    /// Stored self-trade-prevention mode encoded from the original request.
    int stp_mode{ 0 };
    /// Binance-style numeric error code used by async reject tests/logging.
    int binance_error_code{ 0 };
    /// Binance-style error message used by async reject tests/logging.
    std::string binance_error_message;
    /// True when the command targets close-position semantics.
    bool close_position{ false };
};

/// Chooses whether funding is applied before or after same-step matching.
enum class FundingApplyTiming : uint8_t {
    BeforeMatching = 0,
    AfterMatching = 1,
};

/// Declares how a reference/mark price was obtained for diagnostics and logs.
enum class ReferencePriceSource : int32_t {
    None = 0,
    Raw = 1,
    // Reserved enum value for compatibility; current kernel does not emit it.
    Interpolated = 2,
};

/// Captures the outcome of resolving a funding step against mark/reference data.
struct ReferenceFundingResolverDiagnostic {
    /// True when a funding row was applied to account state this step.
    bool applied{ false };
    /// True when a mark price was available to support application/logging.
    bool has_mark_price{ false };
    /// Funding timestamp that was considered for this step.
    uint64_t funding_time{ 0 };
    /// Funding rate applied or observed.
    double funding_rate{ 0.0 };
    /// Source used for the mark price attached to the funding row.
    ReferencePriceSource mark_price_source{ ReferencePriceSource::None };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
