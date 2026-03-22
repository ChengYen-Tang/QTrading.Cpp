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

enum class CoreMode : uint8_t {
    LegacyOnly = 0,
    NewCoreShadow = 1,
    NewCorePrimary = 2,
};

struct StepCompareSnapshot {
    uint64_t ts_exchange{ 0 };
    bool produced_market{ false };
    size_t position_count{ 0 };
    size_t order_count{ 0 };
};

struct StepCompareDiagnostic {
    bool compared{ false };
    bool matched{ true };
    std::string reason;
    StepCompareSnapshot legacy{};
    StepCompareSnapshot candidate{};
};

struct AccountFacadeBridgeDiagnostic {
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    bool shadow_compare_enabled{ false };
    bool v2_explicit_enabled{ false };
};

struct SessionReplayCoexistenceDiagnostic {
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    bool compare_enabled{ false };
    bool fallback_enabled{ false };
};

enum class ReplayStepKind : uint8_t {
    NoOp = 0,
    Market = 1,
    FundingOnly = 2,
    EndOfStream = 3,
};

struct ReplayFrameV2Diagnostic {
    ReplayStepKind kind{ ReplayStepKind::NoOp };
    uint64_t ts_exchange{ 0 };
};

struct TradingSessionCoreV2Diagnostic {
    bool used_v2{ false };
    uint64_t ts_exchange{ 0 };
};

struct BinanceExchangeFacadeBridgeDiagnostic {
    CoreMode requested_mode{ CoreMode::LegacyOnly };
    CoreMode effective_mode{ CoreMode::LegacyOnly };
    bool compare_enabled{ false };
};

enum class EventPublishMode : uint8_t {
    LegacyDirect = 0,
    DualPublishCompare = 1,
};

struct EventPublishCompareSnapshot {
    size_t market_events{ 0 };
    size_t funding_events{ 0 };
    size_t account_events{ 0 };
    size_t position_events{ 0 };
    size_t order_events{ 0 };
};

struct EventPublishCompareDiagnostic {
    bool compared{ false };
    bool matched{ true };
    std::string reason;
    EventPublishCompareSnapshot legacy{};
    EventPublishCompareSnapshot candidate{};
};

struct SideEffectStepSnapshot {
    uint64_t ts_exchange{ 0 };
    uint64_t account_state_version{ 0 };
    std::vector<QTrading::dto::Position> positions;
    std::vector<QTrading::dto::Order> orders;
};

struct SideEffectAdapterConfig {
    std::function<void(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>&)> market_forwarder;
    std::function<void(const std::vector<QTrading::dto::Position>&)> position_forwarder;
    std::function<void(const std::vector<QTrading::dto::Order>&)> order_forwarder;
    std::function<void(const SideEffectStepSnapshot&)> external_hook;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
