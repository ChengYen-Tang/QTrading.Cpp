#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DiagnosticReportFormatter.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

std::string EscapeJson(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (const char ch : input) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string StatusToString(ReplayCompareStatus status)
{
    switch (status) {
    case ReplayCompareStatus::Success:
        return "Success";
    case ReplayCompareStatus::Failed:
        return "Failed";
    case ReplayCompareStatus::Unsupported:
        return "Unsupported";
    case ReplayCompareStatus::Fallback:
        return "Fallback";
    }
    return "Unknown";
}

std::string StatusMarker(ReplayCompareStatus status)
{
    switch (status) {
    case ReplayCompareStatus::Success:
        return "[SUCCESS]";
    case ReplayCompareStatus::Failed:
        return "[FAILED]";
    case ReplayCompareStatus::Unsupported:
        return "[UNSUPPORTED]";
    case ReplayCompareStatus::Fallback:
        return "[FALLBACK]";
    }
    return "[UNKNOWN]";
}

std::optional<ReplayCompareStatus> StatusFromString(std::string_view value)
{
    if (value == "Success") {
        return ReplayCompareStatus::Success;
    }
    if (value == "Failed") {
        return ReplayCompareStatus::Failed;
    }
    if (value == "Unsupported") {
        return ReplayCompareStatus::Unsupported;
    }
    if (value == "Fallback") {
        return ReplayCompareStatus::Fallback;
    }
    return std::nullopt;
}

std::string DomainToString(ReplayMismatchDomain domain)
{
    switch (domain) {
    case ReplayMismatchDomain::State:
        return "State";
    case ReplayMismatchDomain::Event:
        return "Event";
    case ReplayMismatchDomain::LogRow:
        return "LogRow";
    case ReplayMismatchDomain::AsyncAckTimeline:
        return "AsyncAckTimeline";
    case ReplayMismatchDomain::Orchestration:
        return "Orchestration";
    }
    return "Unknown";
}

std::string TriageToString(DiagnosticTriageKind triage)
{
    switch (triage) {
    case DiagnosticTriageKind::None:
        return "None";
    case DiagnosticTriageKind::FeatureGap:
        return "FeatureGap";
    case DiagnosticTriageKind::SemanticDrift:
        return "SemanticDrift";
    case DiagnosticTriageKind::NeedsReview:
        return "NeedsReview";
    }
    return "NeedsReview";
}

std::optional<DiagnosticTriageKind> TriageFromString(std::string_view value)
{
    if (value == "None") {
        return DiagnosticTriageKind::None;
    }
    if (value == "FeatureGap") {
        return DiagnosticTriageKind::FeatureGap;
    }
    if (value == "SemanticDrift") {
        return DiagnosticTriageKind::SemanticDrift;
    }
    if (value == "NeedsReview") {
        return DiagnosticTriageKind::NeedsReview;
    }
    return std::nullopt;
}

bool ContainsCaseInsensitive(std::string value, std::string needle)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(needle) != std::string::npos;
}

std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    const std::string token = "\"" + key + "\":\"";
    const size_t start = json.find(token);
    if (start == std::string::npos) {
        return {};
    }
    const size_t value_begin = start + token.size();
    size_t value_end = value_begin;
    while (value_end < json.size()) {
        const bool escaped_quote = value_end > value_begin && json[value_end - 1] == '\\';
        if (json[value_end] == '"' && !escaped_quote) {
            break;
        }
        ++value_end;
    }
    if (value_end >= json.size()) {
        return {};
    }
    return json.substr(value_begin, value_end - value_begin);
}

std::optional<uint64_t> ExtractJsonUint(const std::string& json, const std::string& key)
{
    const std::string token = "\"" + key + "\":";
    const size_t start = json.find(token);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const size_t value_begin = start + token.size();
    size_t value_end = value_begin;
    while (value_end < json.size() && std::isdigit(static_cast<unsigned char>(json[value_end]))) {
        ++value_end;
    }
    if (value_end == value_begin) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(std::stoull(json.substr(value_begin, value_end - value_begin)));
}

void AppendMismatchJson(std::ostringstream& oss, const ReplayMismatch& mismatch)
{
    oss << "{"
        << "\"domain\":\"" << EscapeJson(DomainToString(mismatch.domain)) << "\","
        << "\"step_index\":" << mismatch.step_index << ","
        << "\"event_seq\":" << mismatch.event_seq << ","
        << "\"row_index\":" << mismatch.row_index << ","
        << "\"ts_exchange\":" << mismatch.ts_exchange << ","
        << "\"field\":\"" << EscapeJson(mismatch.field) << "\","
        << "\"legacy_value\":\"" << EscapeJson(mismatch.legacy_value) << "\","
        << "\"candidate_value\":\"" << EscapeJson(mismatch.candidate_value) << "\","
        << "\"reason\":\"" << EscapeJson(mismatch.reason) << "\""
        << "}";
}

void AppendMismatchText(std::ostringstream& oss, size_t index, const ReplayMismatch& mismatch)
{
    oss << "  mismatch[" << index << "]"
        << " domain=" << DomainToString(mismatch.domain)
        << " step=" << mismatch.step_index
        << " event=" << mismatch.event_seq
        << " row=" << mismatch.row_index
        << " ts_exchange=" << mismatch.ts_exchange
        << " field=" << mismatch.field
        << " legacy=\"" << mismatch.legacy_value << "\""
        << " candidate=\"" << mismatch.candidate_value << "\""
        << " reason=\"" << mismatch.reason << "\"\n";
}

} // namespace

DiagnosticTriageKind DiagnosticReportFormatter::InferTriageKind(const ReplayCompareReport& report)
{
    if (report.status == ReplayCompareStatus::Success) {
        return DiagnosticTriageKind::None;
    }
    if (report.status == ReplayCompareStatus::Unsupported ||
        report.status == ReplayCompareStatus::Fallback) {
        return DiagnosticTriageKind::FeatureGap;
    }
    if (report.status != ReplayCompareStatus::Failed) {
        return DiagnosticTriageKind::NeedsReview;
    }
    if (report.first_mismatch.has_value()) {
        const auto& mismatch = *report.first_mismatch;
        if (ContainsCaseInsensitive(mismatch.reason, "unsupported") ||
            ContainsCaseInsensitive(mismatch.reason, "not implemented") ||
            ContainsCaseInsensitive(mismatch.reason, "not configured")) {
            return DiagnosticTriageKind::FeatureGap;
        }
        return DiagnosticTriageKind::SemanticDrift;
    }
    if (ContainsCaseInsensitive(report.summary, "unsupported") ||
        ContainsCaseInsensitive(report.summary, "not implemented")) {
        return DiagnosticTriageKind::FeatureGap;
    }
    return DiagnosticTriageKind::SemanticDrift;
}

std::string DiagnosticReportFormatter::FormatHumanReadable(
    const ReplayCompareReport& report,
    const LegacyLogRowCompareReport* row_report,
    DiagnosticFormatOptions options)
{
    std::ostringstream oss;
    const auto triage = InferTriageKind(report);
    const size_t limit = (options.mode == DiagnosticReportMode::Summary)
        ? std::max<size_t>(1, options.summary_mismatch_limit)
        : report.mismatches.size();

    oss << StatusMarker(report.status)
        << " scenario=" << report.scenario_name
        << " run_id=" << report.run_id
        << " mismatch_count=" << report.mismatch_count
        << " triage=" << TriageToString(triage)
        << "\n";

    if (report.first_mismatch.has_value()) {
        const auto& first = *report.first_mismatch;
        oss << "first_mismatch"
            << " domain=" << DomainToString(first.domain)
            << " step=" << first.step_index
            << " event=" << first.event_seq
            << " row=" << first.row_index
            << " ts_exchange=" << first.ts_exchange
            << " field=" << first.field
            << "\n";
    } else {
        oss << "first_mismatch none\n";
    }

    if (options.mode == DiagnosticReportMode::Detailed) {
        oss << "steps legacy=" << report.legacy_steps_executed
            << " candidate=" << report.candidate_steps_executed
            << " compared=" << report.compared_steps << "\n";
        oss << "summary \"" << report.summary << "\"\n";
    }

    const size_t rendered = std::min(report.mismatches.size(), limit);
    for (size_t i = 0; i < rendered; ++i) {
        AppendMismatchText(oss, i, report.mismatches[i]);
    }

    if (row_report != nullptr) {
        oss << "row_compare matched=" << (row_report->matched ? "true" : "false")
            << " legacy_rows=" << row_report->legacy_row_count
            << " candidate_rows=" << row_report->candidate_row_count;
        if (row_report->first_divergent_row.has_value()) {
            oss << " first_divergent_row=" << *row_report->first_divergent_row;
        }
        oss << "\n";
    }

    const bool include_rows = options.include_legacy_row_snapshot ||
        options.mode == DiagnosticReportMode::Detailed;
    if (include_rows && !report.legacy_row_snapshot_lines.empty()) {
        oss << "legacy_row_snapshot_lines=" << report.legacy_row_snapshot_lines.size() << "\n";
        for (size_t i = 0; i < report.legacy_row_snapshot_lines.size(); ++i) {
            oss << "  legacy_row_snapshot[" << i << "] "
                << report.legacy_row_snapshot_lines[i] << "\n";
        }
    }

    return oss.str();
}

std::string DiagnosticReportFormatter::SerializeArtifactJson(
    const ReplayCompareReport& report,
    const LegacyLogRowCompareReport* row_report,
    DiagnosticReportMode mode)
{
    std::ostringstream oss;
    const auto triage = InferTriageKind(report);
    const bool detailed = mode == DiagnosticReportMode::Detailed;
    const size_t mismatch_limit = detailed ? report.mismatches.size() : std::min<size_t>(1, report.mismatches.size());

    oss << "{";
    oss << "\"schema_version\":1,";
    oss << "\"scenario_name\":\"" << EscapeJson(report.scenario_name) << "\",";
    oss << "\"run_id\":" << report.run_id << ",";
    oss << "\"status\":\"" << EscapeJson(StatusToString(report.status)) << "\",";
    oss << "\"triage\":\"" << EscapeJson(TriageToString(triage)) << "\",";
    oss << "\"summary\":\"" << EscapeJson(report.summary) << "\",";
    oss << "\"mismatch_count\":" << report.mismatch_count << ",";
    oss << "\"counts\":{"
        << "\"legacy_steps_executed\":" << report.legacy_steps_executed << ","
        << "\"candidate_steps_executed\":" << report.candidate_steps_executed << ","
        << "\"compared_steps\":" << report.compared_steps
        << "},";

    oss << "\"first_mismatch\":";
    if (report.first_mismatch.has_value()) {
        AppendMismatchJson(oss, *report.first_mismatch);
    } else {
        oss << "null";
    }
    oss << ",";

    oss << "\"mismatches\":[";
    for (size_t i = 0; i < mismatch_limit; ++i) {
        if (i > 0) {
            oss << ",";
        }
        AppendMismatchJson(oss, report.mismatches[i]);
    }
    oss << "],";

    if (row_report != nullptr) {
        oss << "\"row_compare\":{"
            << "\"matched\":" << (row_report->matched ? "true" : "false") << ","
            << "\"legacy_row_count\":" << row_report->legacy_row_count << ","
            << "\"candidate_row_count\":" << row_report->candidate_row_count << ","
            << "\"compared_row_count\":" << row_report->compared_row_count << ",";
        oss << "\"first_divergent_row\":";
        if (row_report->first_divergent_row.has_value()) {
            oss << *row_report->first_divergent_row;
        } else {
            oss << "null";
        }
        oss << "},";
    } else {
        oss << "\"row_compare\":null,";
    }

    oss << "\"legacy_row_snapshot_lines\":[";
    for (size_t i = 0; i < report.legacy_row_snapshot_lines.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "\"" << EscapeJson(report.legacy_row_snapshot_lines[i]) << "\"";
    }
    oss << "]";

    oss << "}";
    return oss.str();
}

bool DiagnosticReportFormatter::TryParseArtifactKeyFields(
    const std::string& artifact_json,
    ReplayCompareArtifactKeyFields& out)
{
    const std::string scenario_name = ExtractJsonString(artifact_json, "scenario_name");
    const auto run_id = ExtractJsonUint(artifact_json, "run_id");
    const std::string status = ExtractJsonString(artifact_json, "status");
    const std::string triage = ExtractJsonString(artifact_json, "triage");
    const auto mismatch_count = ExtractJsonUint(artifact_json, "mismatch_count");
    if (scenario_name.empty() || !run_id.has_value() || status.empty() || !mismatch_count.has_value()) {
        return false;
    }

    const auto parsed_status = StatusFromString(status);
    if (!parsed_status.has_value()) {
        return false;
    }

    const auto parsed_triage = TriageFromString(triage);
    if (!parsed_triage.has_value()) {
        return false;
    }

    out.scenario_name = scenario_name;
    out.run_id = *run_id;
    out.status = *parsed_status;
    out.triage = *parsed_triage;
    out.mismatch_count = *mismatch_count;

    const std::string first_field = ExtractJsonString(artifact_json, "field");
    if (!first_field.empty()) {
        out.first_mismatch_field = first_field;
    }

    const auto first_step = ExtractJsonUint(artifact_json, "step_index");
    if (first_step.has_value()) {
        out.first_mismatch_step = *first_step;
    }

    const std::string first_domain = ExtractJsonString(artifact_json, "domain");
    if (!first_domain.empty()) {
        out.first_mismatch_domain = first_domain;
    }

    out.has_legacy_row_snapshot = artifact_json.find("\"legacy_row_snapshot_lines\":[]") == std::string::npos &&
        artifact_json.find("\"legacy_row_snapshot_lines\":[") != std::string::npos;

    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
