#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// Selects which execution core path the facade should route through.
enum class CoreMode : uint8_t {
    LegacyOnly = 0,
    NewCoreShadow = 1,
    NewCorePrimary = 2,
};

/// Compact per-step snapshot used when comparing legacy and candidate core paths.
struct StepCompareSnapshot {
    /// Exchange timestamp associated with the compared step.
    uint64_t ts_exchange{ 0 };
    /// True when the step produced a market payload.
    bool produced_market{ false };
    /// Position count observed after the step.
    size_t position_count{ 0 };
    /// Open-order count observed after the step.
    size_t order_count{ 0 };
};

/// Records whether legacy-vs-candidate step comparison ran and whether it matched.
struct StepCompareDiagnostic {
    /// True when both paths were actually compared.
    bool compared{ false };
    /// True when compared observable results matched.
    bool matched{ true };
    /// First mismatch or skip reason when comparison is not clean.
    std::string reason;
    /// Snapshot captured from the legacy path.
    StepCompareSnapshot legacy{};
    /// Snapshot captured from the candidate path.
    StepCompareSnapshot candidate{};
};

/// Summarizes how account-facing facade calls were routed across transitional cores.
struct AccountFacadeBridgeDiagnostic {
    /// Requested core mode at call entry.
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    /// Actual core mode used after fallback/routing decisions.
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    /// True when a shadow comparison path was enabled.
    bool shadow_compare_enabled{ false };
    /// True when the caller explicitly requested candidate-core routing.
    bool v2_explicit_enabled{ false };
};

/// Describes coexistence decisions between replay stepping and candidate-core rollout.
struct SessionReplayCoexistenceDiagnostic {
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    bool compare_enabled{ false };
    bool fallback_enabled{ false };
};

/// High-level step classification emitted by replay diagnostics.
enum class ReplayStepKind : uint8_t {
    NoOp = 0,
    Market = 1,
    FundingOnly = 2,
    EndOfStream = 3,
};

/// Minimal replay-frame diagnostic for a single stepped frame.
struct ReplayFrameV2Diagnostic {
    ReplayStepKind kind{ ReplayStepKind::NoOp };
    uint64_t ts_exchange{ 0 };
};

/// Minimal diagnostic describing whether the candidate trading-session core was used.
struct TradingSessionCoreV2Diagnostic {
    bool used_v2{ false };
    uint64_t ts_exchange{ 0 };
};

/// Summarizes how top-level BinanceExchange facade calls routed across cores.
struct BinanceExchangeFacadeBridgeDiagnostic {
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    bool compare_enabled{ false };
};

/// Selects how side effects are published during a step.
enum class EventPublishMode : uint8_t {
    LegacyDirect = 0,
    DualPublishCompare = 1,
};

/// Event-count summary used when comparing legacy and candidate publish paths.
struct EventPublishCompareSnapshot {
    size_t market_events{ 0 };
    size_t funding_events{ 0 };
    size_t account_events{ 0 };
    size_t position_events{ 0 };
    size_t order_events{ 0 };
};

/// Records whether event publishing was compared and whether outputs matched.
struct EventPublishCompareDiagnostic {
    bool compared{ false };
    bool matched{ true };
    std::string reason;
    EventPublishCompareSnapshot legacy{};
    EventPublishCompareSnapshot candidate{};
};

/// Full side-effect payload passed to optional external hooks.
struct SideEffectStepSnapshot {
    /// Exchange timestamp for the emitted step.
    uint64_t ts_exchange{ 0 };
    /// Account state version associated with this snapshot.
    uint64_t account_state_version{ 0 };
    /// Position snapshot exposed to observers.
    std::vector<QTrading::dto::Position> positions;
    /// Open-order snapshot exposed to observers.
    std::vector<QTrading::dto::Order> orders;
};

/// Optional callbacks used to mirror runtime side effects outside the facade-owned channels.
struct SideEffectAdapterConfig {
    /// Receives the market payload generated for the current step.
    std::function<void(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>&)> market_forwarder;
    /// Receives the full position snapshot chosen for side-effect publication.
    std::function<void(const std::vector<QTrading::dto::Position>&)> position_forwarder;
    /// Receives the full order snapshot chosen for side-effect publication.
    std::function<void(const std::vector<QTrading::dto::Order>&)> order_forwarder;
    /// Receives the aggregated step snapshot after internal publication work completes.
    std::function<void(const SideEffectStepSnapshot&)> external_hook;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
