#pragma once

#include <cstdint>

namespace QTrading::Infra::Tests::ReplaySemanticsPinning {

inline constexpr uint32_t kInputFixtureVersion = 1u;
inline constexpr uint64_t kPinnedDeterministicSeed = 0x20260321u;

struct ScenarioPin final {
    const char* id;
    uint64_t run_id;
};

inline constexpr ScenarioPin kStatusSnapshotsBeforeEvents{ "status_snapshot_before_events_v1", 7801u };
inline constexpr ScenarioPin kEventModuleOrdering{ "event_module_ordering_v1", 7802u };
inline constexpr ScenarioPin kAsyncAckRejectMapping{ "async_ack_reject_mapping_v1", 7803u };
inline constexpr ScenarioPin kFundingFillSameStepBeforeMatching{ "funding_fill_same_step_before_matching_v1", 7804u };
inline constexpr ScenarioPin kFundingFillSameStepAfterMatching{ "funding_fill_same_step_after_matching_v1", 7805u };
inline constexpr ScenarioPin kLiquidationSyntheticFillContract{ "liquidation_synthetic_fill_contract_v1", 7806u };
inline constexpr ScenarioPin kBalanceDepletionClosesPublicChannels{ "balance_depleted_closes_public_channels_v1", 7807u };

inline constexpr char kStatusSnapshotsBeforeEventsTradeCsv[] = "status_snapshot_before_events_v1.csv";
inline constexpr char kEventModuleOrderingTradeCsv[] = "event_module_ordering_trade_v1.csv";
inline constexpr char kEventModuleOrderingFundingCsv[] = "event_module_ordering_funding_v1.csv";
inline constexpr char kAsyncAckRejectMappingTradeCsv[] = "async_ack_reject_mapping_trade_v1.csv";
inline constexpr char kFundingFillSameStepTradeCsv[] = "funding_fill_same_step_trade_v1.csv";
inline constexpr char kFundingFillSameStepFundingCsv[] = "funding_fill_same_step_funding_v1.csv";
inline constexpr char kLiquidationSyntheticFillTradeCsv[] = "liquidation_synthetic_fill_trade_v1.csv";
inline constexpr char kLiquidationSyntheticFillMarkCsv[] = "liquidation_synthetic_fill_mark_v1.csv";
inline constexpr char kBalanceDepletionTradeCsv[] = "balance_depletion_trade_v1.csv";

} // namespace QTrading::Infra::Tests::ReplaySemanticsPinning
