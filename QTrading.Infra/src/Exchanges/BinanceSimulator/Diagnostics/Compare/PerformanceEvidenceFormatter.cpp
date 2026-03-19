#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceEvidenceFormatter.hpp"

#include <cmath>
#include <iomanip>
#include <optional>
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

std::string ToFixed(double value, int precision = 4)
{
    if (!std::isfinite(value)) {
        return "0.0000";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
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

} // namespace

std::string PerformanceEvidenceFormatter::FormatComparableReport(const PerformanceEvidenceArtifact& artifact)
{
    std::ostringstream oss;
    oss << "[PERF_EVIDENCE] phase=" << artifact.metadata.phase_name
        << " preset=" << artifact.metadata.build_preset
        << " dataset=" << artifact.metadata.dataset_version
        << " baseline=" << artifact.metadata.baseline_version
        << " target=" << artifact.metadata.compare_target_version
        << "\n";

    for (const auto& metric : artifact.metrics) {
        oss << "[PERF_EVIDENCE][metric]"
            << " scenario=" << metric.scenario_name
            << " mode=" << metric.mode_name
            << " p50_ns_per_step=" << ToFixed(metric.p50_ns_per_step)
            << " p95_ns_per_step=" << ToFixed(metric.p95_ns_per_step)
            << " throughput_steps_per_sec=" << ToFixed(metric.throughput_steps_per_sec)
            << " mode_to_mode_ratio="
            << (metric.mode_to_mode_ratio.has_value() ? ToFixed(*metric.mode_to_mode_ratio) : "n/a")
            << "\n";
    }

    oss << "[PERF_EVIDENCE] first_failing_metric="
        << (artifact.first_failing_metric.has_value() ? *artifact.first_failing_metric : "none")
        << "\n";

    for (const auto& link : artifact.semantic_links) {
        oss << "[PERF_EVIDENCE][semantic_link]"
            << " scenario=" << link.scenario_name
            << " semantic_gate=" << link.semantic_gate
            << " semantic_evidence=" << link.semantic_evidence
            << "\n";
    }

    return oss.str();
}

std::string PerformanceEvidenceFormatter::SerializeArtifactJson(const PerformanceEvidenceArtifact& artifact)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"schema_version\":1,";
    oss << "\"metadata\":{"
        << "\"phase_name\":\"" << EscapeJson(artifact.metadata.phase_name) << "\","
        << "\"build_preset\":\"" << EscapeJson(artifact.metadata.build_preset) << "\","
        << "\"test_binary\":\"" << EscapeJson(artifact.metadata.test_binary) << "\","
        << "\"dataset_version\":\"" << EscapeJson(artifact.metadata.dataset_version) << "\","
        << "\"scenario_pack_version\":\"" << EscapeJson(artifact.metadata.scenario_pack_version) << "\","
        << "\"baseline_version\":\"" << EscapeJson(artifact.metadata.baseline_version) << "\","
        << "\"compare_target_version\":\"" << EscapeJson(artifact.metadata.compare_target_version) << "\""
        << "},";

    oss << "\"metrics\":[";
    for (size_t i = 0; i < artifact.metrics.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        const auto& metric = artifact.metrics[i];
        oss << "{"
            << "\"scenario_name\":\"" << EscapeJson(metric.scenario_name) << "\","
            << "\"mode_name\":\"" << EscapeJson(metric.mode_name) << "\","
            << "\"p50_ns_per_step\":" << ToFixed(metric.p50_ns_per_step) << ","
            << "\"p95_ns_per_step\":" << ToFixed(metric.p95_ns_per_step) << ","
            << "\"throughput_steps_per_sec\":" << ToFixed(metric.throughput_steps_per_sec) << ","
            << "\"mode_to_mode_ratio\":";
        if (metric.mode_to_mode_ratio.has_value()) {
            oss << ToFixed(*metric.mode_to_mode_ratio);
        } else {
            oss << "null";
        }
        oss << "}";
    }
    oss << "],";

    oss << "\"first_failing_metric\":";
    if (artifact.first_failing_metric.has_value()) {
        oss << "\"" << EscapeJson(*artifact.first_failing_metric) << "\"";
    } else {
        oss << "null";
    }
    oss << ",";

    oss << "\"semantic_links\":[";
    for (size_t i = 0; i < artifact.semantic_links.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        const auto& link = artifact.semantic_links[i];
        oss << "{"
            << "\"scenario_name\":\"" << EscapeJson(link.scenario_name) << "\","
            << "\"semantic_gate\":\"" << EscapeJson(link.semantic_gate) << "\","
            << "\"semantic_evidence\":\"" << EscapeJson(link.semantic_evidence) << "\""
            << "}";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

bool PerformanceEvidenceFormatter::TryParseKeyFields(
    const std::string& artifact_json,
    PerformanceEvidenceKeyFields& out)
{
    const std::string phase_name = ExtractJsonString(artifact_json, "phase_name");
    const std::string build_preset = ExtractJsonString(artifact_json, "build_preset");
    const std::string dataset_version = ExtractJsonString(artifact_json, "dataset_version");
    const std::string baseline_version = ExtractJsonString(artifact_json, "baseline_version");
    const std::string compare_target_version = ExtractJsonString(artifact_json, "compare_target_version");
    if (phase_name.empty() ||
        build_preset.empty() ||
        dataset_version.empty() ||
        baseline_version.empty() ||
        compare_target_version.empty()) {
        return false;
    }

    out.phase_name = phase_name;
    out.build_preset = build_preset;
    out.dataset_version = dataset_version;
    out.baseline_version = baseline_version;
    out.compare_target_version = compare_target_version;

    const std::string first_failing_metric = ExtractJsonString(artifact_json, "first_failing_metric");
    if (!first_failing_metric.empty()) {
        out.first_failing_metric = first_failing_metric;
    } else {
        out.first_failing_metric.reset();
    }

    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
