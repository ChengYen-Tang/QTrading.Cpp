#pragma once

#include <optional>
#include <string>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

struct PerformanceEvidenceMetric final {
    std::string scenario_name;
    std::string mode_name;
    double p50_ns_per_step{ 0.0 };
    double p95_ns_per_step{ 0.0 };
    double throughput_steps_per_sec{ 0.0 };
    std::optional<double> mode_to_mode_ratio{};
};

struct PerformanceEvidenceMetadata final {
    std::string phase_name;
    std::string build_preset;
    std::string test_binary;
    std::string dataset_version;
    std::string scenario_pack_version;
    std::string baseline_version;
    std::string compare_target_version;
};

struct PerformanceSemanticEvidenceLink final {
    std::string scenario_name;
    std::string semantic_gate;
    std::string semantic_evidence;
};

struct PerformanceEvidenceArtifact final {
    PerformanceEvidenceMetadata metadata{};
    std::vector<PerformanceEvidenceMetric> metrics{};
    std::optional<std::string> first_failing_metric{};
    std::vector<PerformanceSemanticEvidenceLink> semantic_links{};
};

struct PerformanceEvidenceKeyFields final {
    std::string phase_name;
    std::string build_preset;
    std::string dataset_version;
    std::string baseline_version;
    std::string compare_target_version;
    std::optional<std::string> first_failing_metric{};
};

class PerformanceEvidenceFormatter final {
public:
    static std::string FormatComparableReport(const PerformanceEvidenceArtifact& artifact);

    static std::string SerializeArtifactJson(const PerformanceEvidenceArtifact& artifact);

    static bool TryParseKeyFields(
        const std::string& artifact_json,
        PerformanceEvidenceKeyFields& out);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
