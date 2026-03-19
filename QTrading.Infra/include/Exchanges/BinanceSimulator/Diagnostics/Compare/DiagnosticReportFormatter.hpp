#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/LegacyLogRowCompare.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class DiagnosticReportMode : uint8_t {
    Summary = 0,
    Detailed = 1,
};

enum class DiagnosticTriageKind : uint8_t {
    None = 0,
    FeatureGap = 1,
    SemanticDrift = 2,
    NeedsReview = 3,
};

struct DiagnosticFormatOptions {
    DiagnosticReportMode mode{ DiagnosticReportMode::Summary };
    size_t summary_mismatch_limit{ 1 };
    bool include_legacy_row_snapshot{ false };
};

struct ReplayCompareArtifactKeyFields {
    std::string scenario_name;
    uint64_t run_id{ 0 };
    ReplayCompareStatus status{ ReplayCompareStatus::Unsupported };
    DiagnosticTriageKind triage{ DiagnosticTriageKind::NeedsReview };
    uint64_t mismatch_count{ 0 };
    std::optional<std::string> first_mismatch_field{};
    std::optional<uint64_t> first_mismatch_step{};
    std::optional<std::string> first_mismatch_domain{};
    std::optional<ReplayCompareStatus> first_divergent_status{};
    std::optional<uint64_t> first_divergent_step{};
    std::optional<uint64_t> first_divergent_event{};
    std::optional<uint64_t> first_divergent_row{};
    bool has_legacy_row_snapshot{ false };
};

class DiagnosticReportFormatter final {
public:
    static std::string FormatHumanReadable(
        const ReplayCompareReport& report,
        const LegacyLogRowCompareReport* row_report = nullptr,
        DiagnosticFormatOptions options = {});

    static std::string SerializeArtifactJson(
        const ReplayCompareReport& report,
        const LegacyLogRowCompareReport* row_report = nullptr,
        DiagnosticReportMode mode = DiagnosticReportMode::Detailed);

    static bool TryParseArtifactKeyFields(
        const std::string& artifact_json,
        ReplayCompareArtifactKeyFields& out);

    static DiagnosticTriageKind InferTriageKind(const ReplayCompareReport& report);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
