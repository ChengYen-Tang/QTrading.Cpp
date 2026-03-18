#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class ReplayCompareFeature : uint32_t {
    None = 0,
    State = 1u << 0,
    Event = 1u << 1,
    LogRow = 1u << 2,
    AsyncAckTimeline = 1u << 3,
};

struct ReplayCompareFeatureSet {
    uint32_t bits{ static_cast<uint32_t>(ReplayCompareFeature::None) };

    constexpr bool Empty() const
    {
        return bits == static_cast<uint32_t>(ReplayCompareFeature::None);
    }

    constexpr bool Has(ReplayCompareFeature feature) const
    {
        return (bits & static_cast<uint32_t>(feature)) != 0u;
    }

    constexpr void Enable(ReplayCompareFeature feature)
    {
        bits |= static_cast<uint32_t>(feature);
    }

    static constexpr ReplayCompareFeatureSet Core()
    {
        return ReplayCompareFeatureSet{
            static_cast<uint32_t>(ReplayCompareFeature::State) |
            static_cast<uint32_t>(ReplayCompareFeature::Event) |
            static_cast<uint32_t>(ReplayCompareFeature::LogRow)
        };
    }

    static constexpr ReplayCompareFeatureSet All()
    {
        return ReplayCompareFeatureSet{
            static_cast<uint32_t>(ReplayCompareFeature::State) |
            static_cast<uint32_t>(ReplayCompareFeature::Event) |
            static_cast<uint32_t>(ReplayCompareFeature::LogRow) |
            static_cast<uint32_t>(ReplayCompareFeature::AsyncAckTimeline)
        };
    }
};

struct ReplayScenario {
    std::string name;
    std::string dataset_id;
    uint64_t start_ts_exchange{ 0 };
    uint64_t end_ts_exchange{ 0 };
    uint64_t expected_steps{ 0 };

    bool IsDeterministic() const;
};

struct ReplayRunnerConfig {
    ReplayCompareFeatureSet feature_set{ ReplayCompareFeatureSet::Core() };
    bool stop_on_first_mismatch{ true };
    bool allow_fallback_to_legacy{ true };
    uint64_t max_mismatches{ 1 };
};

struct ReplayCoreInputBundle {
    ReplayScenario scenario{};
    ReplayRunnerConfig config{};
    uint64_t run_id{ 0 };
    uint64_t random_seed{ 0 };
    std::shared_ptr<const void> deterministic_input{};
    std::shared_ptr<void> legacy_core_state{};
    std::shared_ptr<void> candidate_core_state{};

    bool HasIndependentCoreStates() const;
};

enum class ReplayCompareStatus : uint8_t {
    Success = 0,
    Failed = 1,
    Unsupported = 2,
    Fallback = 3,
};

enum class ReplayMismatchDomain : uint8_t {
    State = 0,
    Event = 1,
    LogRow = 2,
    AsyncAckTimeline = 3,
    Orchestration = 4,
};

struct ReplayMismatch {
    static constexpr uint64_t kUnspecifiedIndex = std::numeric_limits<uint64_t>::max();

    ReplayMismatchDomain domain{ ReplayMismatchDomain::Orchestration };
    uint64_t step_index{ kUnspecifiedIndex };
    uint64_t event_seq{ kUnspecifiedIndex };
    uint64_t row_index{ kUnspecifiedIndex };
    uint64_t ts_exchange{ 0 };
    std::string field;
    std::string legacy_value;
    std::string candidate_value;
    std::string reason;
};

struct ReplayStepCompareResult {
    uint64_t step_index{ 0 };
    uint64_t ts_exchange{ 0 };
    ReplayCompareStatus status{ ReplayCompareStatus::Success };
    bool compared{ false };
    bool matched{ true };
    bool fallback_to_legacy{ false };
    std::string note;
    std::vector<ReplayMismatch> mismatches;
};

struct ReplayCompareReport {
    std::string scenario_name;
    uint64_t run_id{ 0 };
    ReplayCompareFeatureSet feature_set{};
    ReplayCompareStatus status{ ReplayCompareStatus::Unsupported };
    bool legacy_baseline_preserved{ false };
    uint64_t legacy_steps_executed{ 0 };
    uint64_t candidate_steps_executed{ 0 };
    uint64_t compared_steps{ 0 };
    uint64_t mismatch_count{ 0 };
    std::optional<ReplayMismatch> first_mismatch{};
    std::vector<ReplayMismatch> mismatches;
    std::vector<ReplayStepCompareResult> steps;
    std::vector<std::string> legacy_row_snapshot_lines;
    std::string summary;
};

class DifferentialReplayRunner final {
public:
    struct Callbacks {
        std::function<void(const ReplayCoreInputBundle&, uint64_t)> run_legacy_step;
        std::function<void(const ReplayCoreInputBundle&, uint64_t)> run_candidate_step;
        std::function<ReplayStepCompareResult(const ReplayCoreInputBundle&, uint64_t)> compare_step;
    };

    ReplayCompareReport Run(const ReplayCoreInputBundle& input, const Callbacks& callbacks) const;

private:
    static ReplayCompareStatus PromoteStatus(ReplayCompareStatus aggregate, ReplayCompareStatus step);
    static void NormalizeStepResult(ReplayStepCompareResult& step_result, uint64_t step_index);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
