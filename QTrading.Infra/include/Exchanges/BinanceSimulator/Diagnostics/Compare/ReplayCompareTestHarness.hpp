#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DiagnosticReportFormatter.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/LegacyLogRowCompare.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/StepCompareModel.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class ReplayCompareExecutionMode : uint8_t {
    ProductionSafe = 0,
    TestValidation = 1,
};

struct ReplayCompareScenarioData {
    ReplayScenario scenario{};
    std::vector<StepComparePayload> legacy_steps{};
    std::vector<StepComparePayload> candidate_steps{};
    std::vector<LegacyLogCompareRow> legacy_rows{};
    std::vector<LegacyLogCompareRow> candidate_rows{};
    std::vector<std::string> legacy_row_snapshot_lines{};
};

struct ReplayCompareTestHarnessOptions {
    ReplayCompareExecutionMode execution_mode{ ReplayCompareExecutionMode::ProductionSafe };
    ReplayCompareFeatureSet feature_set{ ReplayCompareFeatureSet::Core() };
    bool align_step_boundary_by_common_payload{ true };
    StepCompareRules step_rules{};
    LegacyLogRowCompareRules row_rules{};
    bool generate_artifact{ false };
    DiagnosticReportMode human_readable_mode{ DiagnosticReportMode::Summary };
    DiagnosticReportMode artifact_mode{ DiagnosticReportMode::Detailed };
    bool include_legacy_row_snapshot{ false };
    std::vector<std::string> scenario_name_filters{};
    uint64_t run_id_override{ 0 };
    uint64_t random_seed{ 0 };
};

struct ReplayCompareHarnessResult {
    ReplayCompareReport report{};
    std::optional<LegacyLogRowCompareReport> row_report{};
    std::string human_readable_report{};
    std::optional<std::string> artifact_json{};
    bool dual_run_enabled{ false };
    bool heavy_artifact_generated{ false };
};

struct ReplayCompareScenarioPackResult {
    std::vector<ReplayCompareHarnessResult> scenario_results{};
    uint64_t total_mismatch_count{ 0 };
    size_t failed_scenarios{ 0 };
    size_t unsupported_scenarios{ 0 };
    size_t fallback_scenarios{ 0 };
    bool all_passed{ false };
};

class ReplayCompareTestHarness final {
public:
    ReplayCompareHarnessResult RunSingleScenario(
        const ReplayCompareScenarioData& scenario_data,
        ReplayCompareTestHarnessOptions options = {}) const;

    ReplayCompareHarnessResult RunSingleScenarioByFeatureFilter(
        const ReplayCompareScenarioData& scenario_data,
        ReplayCompareFeatureSet feature_filter,
        ReplayCompareTestHarnessOptions options = {}) const;

    ReplayCompareScenarioPackResult RunScenarioPack(
        const std::vector<ReplayCompareScenarioData>& scenarios,
        ReplayCompareTestHarnessOptions options = {}) const;

    ReplayCompareScenarioPackResult RunScenarioPackByFeatureFilter(
        const std::vector<ReplayCompareScenarioData>& scenarios,
        ReplayCompareFeatureSet feature_filter,
        ReplayCompareTestHarnessOptions options = {}) const;

    static ReplayCompareFeatureSet BuildFeatureSet(std::initializer_list<ReplayCompareFeature> features);
    static ReplayCompareFeatureSet StateOnlyFeatureSet();
    static ReplayCompareFeatureSet EventOnlyFeatureSet();
    static ReplayCompareFeatureSet RowOnlyFeatureSet();
    static ReplayCompareFeatureSet AckOnlyFeatureSet();

private:
    static ReplayCompareTestHarnessOptions NormalizeOptions(ReplayCompareTestHarnessOptions options);
    static StepCompareRules BuildEffectiveStepRules(
        const StepCompareRules& base_rules,
        ReplayCompareFeatureSet feature_set);
    static bool ShouldIncludeScenario(
        const ReplayCompareScenarioData& scenario_data,
        const ReplayCompareTestHarnessOptions& options);
    static void MergeRowCompareIntoReport(
        ReplayCompareReport& report,
        const LegacyLogRowCompareReport& row_report);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
