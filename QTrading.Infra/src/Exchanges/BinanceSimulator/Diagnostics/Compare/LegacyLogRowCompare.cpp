#include "Exchanges/BinanceSimulator/Diagnostics/Compare/LegacyLogRowCompare.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

using RowRef = std::reference_wrapper<const LegacyLogCompareRow>;

bool InScope(
    int32_t module_id,
    const std::vector<int32_t>& scope_ids)
{
    if (scope_ids.empty()) {
        return true;
    }
    return std::find(scope_ids.begin(), scope_ids.end(), module_id) != scope_ids.end();
}

std::vector<RowRef> NormalizeRows(
    const std::vector<LegacyLogCompareRow>& rows,
    const LegacyLogRowCompareRules& rules)
{
    std::vector<RowRef> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        if (InScope(row.module_id, rules.module_scope_ids)) {
            out.push_back(std::cref(row));
        }
    }

    auto arrival_less = [](const RowRef& lhs, const RowRef& rhs) {
        return lhs.get().arrival_index < rhs.get().arrival_index;
    };

    auto business_less = [](const RowRef& lhs, const RowRef& rhs) {
        if (lhs.get().step_seq != rhs.get().step_seq) {
            return lhs.get().step_seq < rhs.get().step_seq;
        }
        if (lhs.get().event_seq != rhs.get().event_seq) {
            return lhs.get().event_seq < rhs.get().event_seq;
        }
        if (lhs.get().module_id != rhs.get().module_id) {
            return lhs.get().module_id < rhs.get().module_id;
        }
        if (lhs.get().batch_boundary != rhs.get().batch_boundary) {
            return lhs.get().batch_boundary < rhs.get().batch_boundary;
        }
        return lhs.get().arrival_index < rhs.get().arrival_index;
    };

    if (rules.ordering == LegacyLogRowOrderingRule::Arrival) {
        std::stable_sort(out.begin(), out.end(), arrival_less);
    } else {
        std::stable_sort(out.begin(), out.end(), business_less);
    }
    return out;
}

std::map<std::string, std::string> BuildPayloadMap(const LegacyLogCompareRow& row)
{
    std::map<std::string, std::string> out;
    for (const auto& field : row.payload_fields) {
        out[field.key] = field.value;
    }
    return out;
}

std::string ToString(LegacyLogRowKind kind)
{
    switch (kind) {
    case LegacyLogRowKind::Snapshot:
        return "Snapshot";
    case LegacyLogRowKind::Event:
        return "Event";
    }
    return "Unknown";
}

void AddMismatch(
    LegacyLogRowCompareReport& report,
    uint64_t row_index,
    const LegacyLogCompareRow* legacy_row,
    const LegacyLogCompareRow* candidate_row,
    std::string_view field,
    const std::string& legacy_value,
    const std::string& candidate_value,
    std::string_view reason)
{
    ReplayMismatch mismatch{};
    mismatch.domain = ReplayMismatchDomain::LogRow;
    mismatch.row_index = row_index;
    mismatch.field = std::string(field);
    mismatch.legacy_value = legacy_value;
    mismatch.candidate_value = candidate_value;
    mismatch.reason = std::string(reason);

    if (legacy_row) {
        mismatch.step_index = legacy_row->step_seq;
        mismatch.event_seq = legacy_row->event_seq;
        mismatch.ts_exchange = legacy_row->ts_exchange;
    } else if (candidate_row) {
        mismatch.step_index = candidate_row->step_seq;
        mismatch.event_seq = candidate_row->event_seq;
        mismatch.ts_exchange = candidate_row->ts_exchange;
    }

    if (!report.first_divergent_row.has_value()) {
        report.first_divergent_row = row_index;
    }
    if (!report.first_mismatch.has_value()) {
        report.first_mismatch = mismatch;
    }
    report.mismatches.push_back(std::move(mismatch));
    report.matched = false;
}

void ComparePayload(
    LegacyLogRowCompareReport& report,
    uint64_t row_index,
    const LegacyLogCompareRow& legacy_row,
    const LegacyLogCompareRow& candidate_row)
{
    const auto legacy_payload = BuildPayloadMap(legacy_row);
    const auto candidate_payload = BuildPayloadMap(candidate_row);

    std::set<std::string> keys;
    for (const auto& [key, _] : legacy_payload) {
        keys.insert(key);
    }
    for (const auto& [key, _] : candidate_payload) {
        keys.insert(key);
    }

    for (const auto& key : keys) {
        const auto legacy_it = legacy_payload.find(key);
        const auto candidate_it = candidate_payload.find(key);
        if (legacy_it == legacy_payload.end()) {
            AddMismatch(
                report,
                row_index,
                &legacy_row,
                &candidate_row,
                "payload." + key,
                "<absent>",
                candidate_it->second,
                "payload presence mismatch");
            continue;
        }
        if (candidate_it == candidate_payload.end()) {
            AddMismatch(
                report,
                row_index,
                &legacy_row,
                &candidate_row,
                "payload." + key,
                legacy_it->second,
                "<absent>",
                "payload absence mismatch");
            continue;
        }
        if (legacy_it->second != candidate_it->second) {
            AddMismatch(
                report,
                row_index,
                &legacy_row,
                &candidate_row,
                "payload." + key,
                legacy_it->second,
                candidate_it->second,
                "payload value mismatch");
        }
    }
}

void CompareRowPair(
    LegacyLogRowCompareReport& report,
    uint64_t row_index,
    const LegacyLogCompareRow& legacy_row,
    const LegacyLogCompareRow& candidate_row,
    const LegacyLogRowCompareRules& rules)
{
    if (rules.strict_module_ordering && legacy_row.module_id != candidate_row.module_id) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.module_id",
            std::to_string(legacy_row.module_id),
            std::to_string(candidate_row.module_id),
            "module ordering/id mismatch");
    }

    if (legacy_row.module_name != candidate_row.module_name) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.module_name",
            legacy_row.module_name,
            candidate_row.module_name,
            "module name mismatch");
    }

    if (rules.strict_ts_exchange && legacy_row.ts_exchange != candidate_row.ts_exchange) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.ts_exchange",
            std::to_string(legacy_row.ts_exchange),
            std::to_string(candidate_row.ts_exchange),
            "timestamp semantics mismatch");
    }

    if (rules.strict_ts_local && legacy_row.ts_local != candidate_row.ts_local) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.ts_local",
            std::to_string(legacy_row.ts_local),
            std::to_string(candidate_row.ts_local),
            "timestamp semantics mismatch");
    }

    if (rules.strict_step_seq && legacy_row.step_seq != candidate_row.step_seq) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.step_seq",
            std::to_string(legacy_row.step_seq),
            std::to_string(candidate_row.step_seq),
            "sequence semantics mismatch");
    }

    if (rules.strict_event_seq && legacy_row.event_seq != candidate_row.event_seq) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.event_seq",
            std::to_string(legacy_row.event_seq),
            std::to_string(candidate_row.event_seq),
            "sequence semantics mismatch");
    }

    if (rules.strict_row_kind && legacy_row.row_kind != candidate_row.row_kind) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.kind",
            ToString(legacy_row.row_kind),
            ToString(candidate_row.row_kind),
            "snapshot/event row kind mismatch");
    }

    if (rules.strict_batch_boundary && legacy_row.batch_boundary != candidate_row.batch_boundary) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.batch_boundary",
            std::to_string(legacy_row.batch_boundary),
            std::to_string(candidate_row.batch_boundary),
            "batch boundary mismatch");
    }

    if (legacy_row.symbol != candidate_row.symbol) {
        AddMismatch(
            report,
            row_index,
            &legacy_row,
            &candidate_row,
            "row.symbol",
            legacy_row.symbol,
            candidate_row.symbol,
            "symbol mismatch");
    }

    if (rules.strict_payload_fields) {
        ComparePayload(report, row_index, legacy_row, candidate_row);
    }
}

} // namespace

LegacyLogRowCompareReport LegacyLogRowComparer::Compare(
    const std::vector<LegacyLogCompareRow>& legacy_rows,
    const std::vector<LegacyLogCompareRow>& candidate_rows,
    const LegacyLogRowCompareRules& rules) const
{
    LegacyLogRowCompareReport report{};
    const auto normalized_legacy = NormalizeRows(legacy_rows, rules);
    const auto normalized_candidate = NormalizeRows(candidate_rows, rules);

    report.legacy_row_count = normalized_legacy.size();
    report.candidate_row_count = normalized_candidate.size();

    if (normalized_legacy.size() != normalized_candidate.size()) {
        AddMismatch(
            report,
            0,
            normalized_legacy.empty() ? nullptr : &normalized_legacy.front().get(),
            normalized_candidate.empty() ? nullptr : &normalized_candidate.front().get(),
            "row.count",
            std::to_string(normalized_legacy.size()),
            std::to_string(normalized_candidate.size()),
            "row count mismatch");
    }

    const size_t common_count = std::min(normalized_legacy.size(), normalized_candidate.size());
    for (size_t i = 0; i < common_count; ++i) {
        CompareRowPair(
            report,
            i,
            normalized_legacy[i].get(),
            normalized_candidate[i].get(),
            rules);
        ++report.compared_row_count;
    }

    if (normalized_legacy.size() > common_count) {
        for (size_t i = common_count; i < normalized_legacy.size(); ++i) {
            AddMismatch(
                report,
                i,
                &normalized_legacy[i].get(),
                nullptr,
                "row.presence",
                normalized_legacy[i].get().module_name,
                "<absent>",
                "candidate row missing");
        }
    } else if (normalized_candidate.size() > common_count) {
        for (size_t i = common_count; i < normalized_candidate.size(); ++i) {
            AddMismatch(
                report,
                i,
                nullptr,
                &normalized_candidate[i].get(),
                "row.absence",
                "<absent>",
                normalized_candidate[i].get().module_name,
                "candidate has extra row");
        }
    }

    return report;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
