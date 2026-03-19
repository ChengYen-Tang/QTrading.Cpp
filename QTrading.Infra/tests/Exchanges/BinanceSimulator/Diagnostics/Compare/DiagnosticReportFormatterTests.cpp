#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DiagnosticReportFormatter.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::ReplayMismatch MakeMismatch(
    ReplayCompare::ReplayMismatchDomain domain,
    uint64_t step_index,
    uint64_t event_seq,
    uint64_t row_index,
    uint64_t ts_exchange,
    std::string field,
    std::string legacy_value,
    std::string candidate_value,
    std::string reason)
{
    ReplayCompare::ReplayMismatch mismatch{};
    mismatch.domain = domain;
    mismatch.step_index = step_index;
    mismatch.event_seq = event_seq;
    mismatch.row_index = row_index;
    mismatch.ts_exchange = ts_exchange;
    mismatch.field = std::move(field);
    mismatch.legacy_value = std::move(legacy_value);
    mismatch.candidate_value = std::move(candidate_value);
    mismatch.reason = std::move(reason);
    return mismatch;
}

ReplayCompare::ReplayCompareReport BuildFailedReport()
{
    ReplayCompare::ReplayCompareReport report{};
    report.scenario_name = "dual-symbol-holes";
    report.run_id = 20260318;
    report.status = ReplayCompare::ReplayCompareStatus::Failed;
    report.legacy_steps_executed = 10;
    report.candidate_steps_executed = 10;
    report.compared_steps = 10;
    report.summary = "stopped at first mismatch";
    report.mismatches = {
        MakeMismatch(
            ReplayCompare::ReplayMismatchDomain::State,
            7,
            3,
            0,
            1700000010000ULL,
            "account.total_cash_balance",
            "1000",
            "998.5",
            "state drift"),
        MakeMismatch(
            ReplayCompare::ReplayMismatchDomain::Event,
            7,
            4,
            0,
            1700000010000ULL,
            "event[1].amount",
            "-0.12",
            "-0.11",
            "funding effect mismatch"),
        MakeMismatch(
            ReplayCompare::ReplayMismatchDomain::LogRow,
            7,
            4,
            15,
            1700000010000ULL,
            "payload.step_seq",
            "7",
            "8",
            "row contract mismatch"),
    };
    report.first_mismatch = report.mismatches.front();
    report.first_divergent_step = 7;
    report.first_divergent_event_seq = 3;
    report.first_divergent_row = 15;
    report.first_divergent_status = ReplayCompare::ReplayCompareStatus::Failed;
    report.first_divergent_reason = "state drift";
    report.mismatch_count = report.mismatches.size();
    report.legacy_row_snapshot_lines = {
        "row[14] AccountEvent ...",
        "row[15] FundingEvent ..."
    };
    return report;
}

ReplayCompare::LegacyLogRowCompareReport BuildRowReport()
{
    ReplayCompare::LegacyLogRowCompareReport row_report{};
    row_report.matched = false;
    row_report.legacy_row_count = 20;
    row_report.candidate_row_count = 20;
    row_report.compared_row_count = 16;
    row_report.first_divergent_row = 15;
    row_report.first_mismatch = MakeMismatch(
        ReplayCompare::ReplayMismatchDomain::LogRow,
        7,
        4,
        15,
        1700000010000ULL,
        "payload.step_seq",
        "7",
        "8",
        "row contract mismatch");
    row_report.mismatches = { *row_report.first_mismatch };
    return row_report;
}

size_t CountSubstr(const std::string& text, const std::string& needle)
{
    if (needle.empty()) {
        return 0;
    }
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

} // namespace

TEST(DiagnosticReportFormatterTests, SummaryAndDetailedOutputsAreDifferent)
{
    const auto report = BuildFailedReport();
    const auto row_report = BuildRowReport();

    ReplayCompare::DiagnosticFormatOptions summary_opts{};
    summary_opts.mode = ReplayCompare::DiagnosticReportMode::Summary;
    summary_opts.summary_mismatch_limit = 1;
    summary_opts.include_legacy_row_snapshot = false;
    const std::string summary = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(
        report,
        &row_report,
        summary_opts);

    ReplayCompare::DiagnosticFormatOptions detailed_opts{};
    detailed_opts.mode = ReplayCompare::DiagnosticReportMode::Detailed;
    const std::string detailed = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(
        report,
        &row_report,
        detailed_opts);

    EXPECT_NE(summary, detailed);
    EXPECT_NE(summary.find("[FAILED]"), std::string::npos);
    EXPECT_NE(summary.find("scenario=dual-symbol-holes"), std::string::npos);
    EXPECT_EQ(summary.find("legacy_row_snapshot[0]"), std::string::npos);

    EXPECT_NE(detailed.find("mismatch[2]"), std::string::npos);
    EXPECT_NE(detailed.find("legacy_row_snapshot[0]"), std::string::npos);
    EXPECT_NE(detailed.find("row_compare"), std::string::npos);
}

TEST(DiagnosticReportFormatterTests, FailedUnsupportedAndFallbackStatusesHaveClearMarkers)
{
    auto report = BuildFailedReport();
    auto text = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(report);
    EXPECT_NE(text.find("[FAILED]"), std::string::npos);
    EXPECT_NE(text.find("triage=SemanticDrift"), std::string::npos);

    report.status = ReplayCompare::ReplayCompareStatus::Unsupported;
    report.summary = "feature unsupported";
    report.mismatches.clear();
    report.first_mismatch.reset();
    report.mismatch_count = 0;
    text = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(report);
    EXPECT_NE(text.find("[UNSUPPORTED]"), std::string::npos);
    EXPECT_NE(text.find("triage=FeatureGap"), std::string::npos);

    report.status = ReplayCompare::ReplayCompareStatus::Fallback;
    text = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(report);
    EXPECT_NE(text.find("[FALLBACK]"), std::string::npos);
}

TEST(DiagnosticReportFormatterTests, OutputContainsStateEventAndRowDomainDetails)
{
    const auto report = BuildFailedReport();
    ReplayCompare::DiagnosticFormatOptions opts{};
    opts.mode = ReplayCompare::DiagnosticReportMode::Detailed;
    const std::string text = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(report, nullptr, opts);

    EXPECT_NE(text.find("domain=State"), std::string::npos);
    EXPECT_NE(text.find("domain=Event"), std::string::npos);
    EXPECT_NE(text.find("domain=LogRow"), std::string::npos);
}

TEST(DiagnosticReportFormatterTests, OutputShowsScenarioStepAndFirstMismatchLocation)
{
    const auto report = BuildFailedReport();
    const std::string text = ReplayCompare::DiagnosticReportFormatter::FormatHumanReadable(report);

    EXPECT_NE(text.find("scenario=dual-symbol-holes"), std::string::npos);
    EXPECT_NE(text.find("step=7"), std::string::npos);
    EXPECT_NE(text.find("first_divergence status=Failed"), std::string::npos);
    EXPECT_NE(text.find("field=account.total_cash_balance"), std::string::npos);
    EXPECT_NE(text.find("mismatch_count=3"), std::string::npos);
}

TEST(DiagnosticReportFormatterTests, ArtifactJsonCanBeParsedForKeyFields)
{
    const auto report = BuildFailedReport();
    const auto row_report = BuildRowReport();
    const std::string artifact = ReplayCompare::DiagnosticReportFormatter::SerializeArtifactJson(
        report,
        &row_report,
        ReplayCompare::DiagnosticReportMode::Detailed);

    ReplayCompare::ReplayCompareArtifactKeyFields key_fields{};
    ASSERT_TRUE(ReplayCompare::DiagnosticReportFormatter::TryParseArtifactKeyFields(artifact, key_fields));
    EXPECT_EQ(key_fields.scenario_name, "dual-symbol-holes");
    EXPECT_EQ(key_fields.run_id, 20260318u);
    EXPECT_EQ(key_fields.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_EQ(key_fields.triage, ReplayCompare::DiagnosticTriageKind::SemanticDrift);
    EXPECT_EQ(key_fields.mismatch_count, 3u);
    ASSERT_TRUE(key_fields.first_mismatch_field.has_value());
    EXPECT_EQ(*key_fields.first_mismatch_field, "account.total_cash_balance");
    ASSERT_TRUE(key_fields.first_mismatch_step.has_value());
    EXPECT_EQ(*key_fields.first_mismatch_step, 7u);
    ASSERT_TRUE(key_fields.first_mismatch_domain.has_value());
    EXPECT_EQ(*key_fields.first_mismatch_domain, "State");
    ASSERT_TRUE(key_fields.first_divergent_status.has_value());
    EXPECT_EQ(*key_fields.first_divergent_status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(key_fields.first_divergent_step.has_value());
    EXPECT_EQ(*key_fields.first_divergent_step, 7u);
    ASSERT_TRUE(key_fields.first_divergent_event.has_value());
    EXPECT_EQ(*key_fields.first_divergent_event, 3u);
    ASSERT_TRUE(key_fields.first_divergent_row.has_value());
    EXPECT_EQ(*key_fields.first_divergent_row, 15u);
    EXPECT_TRUE(key_fields.has_legacy_row_snapshot);
}

TEST(DiagnosticReportFormatterTests, ArtifactSummaryModeTruncatesMismatchArray)
{
    const auto report = BuildFailedReport();

    const std::string summary_artifact = ReplayCompare::DiagnosticReportFormatter::SerializeArtifactJson(
        report,
        nullptr,
        ReplayCompare::DiagnosticReportMode::Summary);
    const std::string detailed_artifact = ReplayCompare::DiagnosticReportFormatter::SerializeArtifactJson(
        report,
        nullptr,
        ReplayCompare::DiagnosticReportMode::Detailed);

    EXPECT_EQ(CountSubstr(summary_artifact, "\"domain\":"), 2u); // first_mismatch + one array entry
    EXPECT_EQ(CountSubstr(detailed_artifact, "\"domain\":"), 4u); // first_mismatch + three array entries
}

TEST(DiagnosticReportFormatterTests, TriageDifferentiatesFeatureGapAndSemanticDrift)
{
    auto report = BuildFailedReport();
    EXPECT_EQ(
        ReplayCompare::DiagnosticReportFormatter::InferTriageKind(report),
        ReplayCompare::DiagnosticTriageKind::SemanticDrift);

    report.status = ReplayCompare::ReplayCompareStatus::Unsupported;
    report.summary = "candidate not implemented";
    EXPECT_EQ(
        ReplayCompare::DiagnosticReportFormatter::InferTriageKind(report),
        ReplayCompare::DiagnosticTriageKind::FeatureGap);
}
