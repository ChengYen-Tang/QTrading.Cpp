#include <gtest/gtest.h>

#include <string>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceEvidenceFormatter.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::PerformanceEvidenceArtifact BuildEvidenceArtifact()
{
    ReplayCompare::PerformanceEvidenceArtifact artifact{};
    artifact.metadata.phase_name = "milestone6-phase5";
    artifact.metadata.build_preset = "x64-debug";
    artifact.metadata.test_binary = "out/build/x64-debug/QTrading.Infra/tests/QTrading.Infra.Tests.exe";
    artifact.metadata.dataset_version = "v2-legacy-replay-pack-20260319";
    artifact.metadata.scenario_pack_version = "milestone5-v2-session-replay-pack-20260319";
    artifact.metadata.baseline_version = "legacy-only@20260319";
    artifact.metadata.compare_target_version = "shadow-compare@20260319";

    artifact.metrics = {
        ReplayCompare::PerformanceEvidenceMetric{
            "v2-vs-legacy.basis-stress",
            "legacy-only",
            1200.0,
            1800.0,
            833.3333,
            std::nullopt
        },
        ReplayCompare::PerformanceEvidenceMetric{
            "v2-vs-legacy.basis-stress",
            "shadow-compare",
            1550.0,
            2100.0,
            645.1613,
            1.2917
        }
    };

    artifact.first_failing_metric = "latency_ratio_vs_legacy";
    artifact.semantic_links = {
        ReplayCompare::PerformanceSemanticEvidenceLink{
            "v2-vs-legacy.basis-stress",
            "DeterministicReplayFixture.DualSymbolHolesGoldenReplayProducesExpectedLegacyLogContractSnapshots",
            "mismatch_count=0; first_divergence=none"
        }
    };
    return artifact;
}

} // namespace

TEST(PerformanceEvidenceFormatterTests, ComparableReportContainsP50P95ThroughputAndModeRatio)
{
    const auto artifact = BuildEvidenceArtifact();
    const std::string report = ReplayCompare::PerformanceEvidenceFormatter::FormatComparableReport(artifact);

    EXPECT_NE(report.find("p50_ns_per_step="), std::string::npos);
    EXPECT_NE(report.find("p95_ns_per_step="), std::string::npos);
    EXPECT_NE(report.find("throughput_steps_per_sec="), std::string::npos);
    EXPECT_NE(report.find("mode_to_mode_ratio="), std::string::npos);
}

TEST(PerformanceEvidenceFormatterTests, ArtifactJsonContainsMetadataBaselineAndCompareTargetVersion)
{
    const auto artifact = BuildEvidenceArtifact();
    const std::string json = ReplayCompare::PerformanceEvidenceFormatter::SerializeArtifactJson(artifact);

    EXPECT_NE(json.find("\"build_preset\":\"x64-debug\""), std::string::npos);
    EXPECT_NE(json.find("\"test_binary\":\"out/build/x64-debug/QTrading.Infra/tests/QTrading.Infra.Tests.exe\""), std::string::npos);
    EXPECT_NE(json.find("\"dataset_version\":\"v2-legacy-replay-pack-20260319\""), std::string::npos);
    EXPECT_NE(json.find("\"scenario_pack_version\":\"milestone5-v2-session-replay-pack-20260319\""), std::string::npos);
    EXPECT_NE(json.find("\"baseline_version\":\"legacy-only@20260319\""), std::string::npos);
    EXPECT_NE(json.find("\"compare_target_version\":\"shadow-compare@20260319\""), std::string::npos);
}

TEST(PerformanceEvidenceFormatterTests, ArtifactJsonContainsFirstFailingMetricAndSemanticEvidenceMapping)
{
    const auto artifact = BuildEvidenceArtifact();
    const std::string json = ReplayCompare::PerformanceEvidenceFormatter::SerializeArtifactJson(artifact);

    EXPECT_NE(json.find("\"first_failing_metric\":\"latency_ratio_vs_legacy\""), std::string::npos);
    EXPECT_NE(json.find("\"semantic_links\":"), std::string::npos);
    EXPECT_NE(
        json.find("\"semantic_gate\":\"DeterministicReplayFixture.DualSymbolHolesGoldenReplayProducesExpectedLegacyLogContractSnapshots\""),
        std::string::npos);
    EXPECT_NE(json.find("\"semantic_evidence\":\"mismatch_count=0; first_divergence=none\""), std::string::npos);
}

TEST(PerformanceEvidenceFormatterTests, TryParseKeyFieldsReadsComparableEvidenceCoreMetadata)
{
    const auto artifact = BuildEvidenceArtifact();
    const std::string json = ReplayCompare::PerformanceEvidenceFormatter::SerializeArtifactJson(artifact);

    ReplayCompare::PerformanceEvidenceKeyFields key_fields{};
    ASSERT_TRUE(ReplayCompare::PerformanceEvidenceFormatter::TryParseKeyFields(json, key_fields));
    EXPECT_EQ(key_fields.phase_name, "milestone6-phase5");
    EXPECT_EQ(key_fields.build_preset, "x64-debug");
    EXPECT_EQ(key_fields.dataset_version, "v2-legacy-replay-pack-20260319");
    EXPECT_EQ(key_fields.baseline_version, "legacy-only@20260319");
    EXPECT_EQ(key_fields.compare_target_version, "shadow-compare@20260319");
    ASSERT_TRUE(key_fields.first_failing_metric.has_value());
    EXPECT_EQ(*key_fields.first_failing_metric, "latency_ratio_vs_legacy");
}
