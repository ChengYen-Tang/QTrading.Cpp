#include "Exchanges/BinanceSimulator/Diagnostics/Compare/StepCompareModel.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

std::string ToString(ReplayCompareStatus status)
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

std::string ToString(ReplayEventType type)
{
    switch (type) {
    case ReplayEventType::Fill:
        return "Fill";
    case ReplayEventType::Funding:
        return "Funding";
    case ReplayEventType::Rejection:
        return "Rejection";
    case ReplayEventType::Liquidation:
        return "Liquidation";
    case ReplayEventType::AsyncAck:
        return "AsyncAck";
    }
    return "Unknown";
}

std::string ToString(bool value)
{
    return value ? "true" : "false";
}

std::string ToString(uint64_t value)
{
    return std::to_string(value);
}

std::string ToString(int32_t value)
{
    return std::to_string(value);
}

std::string ToString(double value)
{
    std::ostringstream oss;
    oss << std::setprecision(15) << value;
    return oss.str();
}

bool ApproximatelyEqual(double lhs, double rhs, double abs_tolerance)
{
    return std::fabs(lhs - rhs) <= abs_tolerance;
}

void AddMismatch(
    ReplayStepCompareResult& out,
    ReplayMismatchDomain domain,
    std::string_view field,
    const std::string& legacy_value,
    const std::string& candidate_value,
    std::string_view reason,
    uint64_t ts_exchange,
    uint64_t event_seq = ReplayMismatch::kUnspecifiedIndex)
{
    ReplayMismatch mismatch{};
    mismatch.domain = domain;
    mismatch.field = std::string(field);
    mismatch.legacy_value = legacy_value;
    mismatch.candidate_value = candidate_value;
    mismatch.reason = std::string(reason);
    mismatch.ts_exchange = ts_exchange;
    mismatch.event_seq = event_seq;
    out.mismatches.push_back(std::move(mismatch));
}

template <typename T>
std::string Stringify(const T& value)
{
    return ToString(value);
}

template <>
std::string Stringify<std::string>(const std::string& value)
{
    return value;
}

template <typename T>
void CompareStrictField(
    ReplayStepCompareResult& out,
    ReplayMismatchDomain domain,
    std::string_view field,
    const T& legacy_value,
    const T& candidate_value,
    uint64_t ts_exchange,
    uint64_t event_seq = ReplayMismatch::kUnspecifiedIndex)
{
    if (legacy_value == candidate_value) {
        return;
    }

    AddMismatch(
        out,
        domain,
        field,
        Stringify<T>(legacy_value),
        Stringify<T>(candidate_value),
        "strict mismatch",
        ts_exchange,
        event_seq);
}

void CompareFloatField(
    ReplayStepCompareResult& out,
    ReplayMismatchDomain domain,
    std::string_view field,
    double legacy_value,
    double candidate_value,
    double tolerance,
    uint64_t ts_exchange,
    uint64_t event_seq = ReplayMismatch::kUnspecifiedIndex)
{
    if (ApproximatelyEqual(legacy_value, candidate_value, tolerance)) {
        return;
    }

    AddMismatch(
        out,
        domain,
        field,
        ToString(legacy_value),
        ToString(candidate_value),
        "float mismatch exceeds tolerance",
        ts_exchange,
        event_seq);
}

ReplayMismatchDomain DomainForEventField(
    ReplayEventType event_type,
    bool timeline_field)
{
    if (event_type == ReplayEventType::AsyncAck && timeline_field) {
        return ReplayMismatchDomain::AsyncAckTimeline;
    }
    return ReplayMismatchDomain::Event;
}

bool IsEventTypeEnabled(
    ReplayEventType type,
    const EventCompareRules& rules)
{
    switch (type) {
    case ReplayEventType::Fill:
        return rules.compare_fill;
    case ReplayEventType::Funding:
        return rules.compare_funding;
    case ReplayEventType::Rejection:
    case ReplayEventType::Liquidation:
        return rules.compare_rejection_liquidation;
    case ReplayEventType::AsyncAck:
        return rules.compare_async_ack_timeline;
    }
    return true;
}

void CompareState(
    ReplayStepCompareResult& out,
    const StepStateComparePayload& legacy,
    const StepStateComparePayload& candidate,
    const StepCompareRules& rules)
{
    const uint64_t ts_exchange = legacy.ts_exchange;

    if (rules.state.strict_step_seq) {
        CompareStrictField<uint64_t>(
            out,
            ReplayMismatchDomain::State,
            "state.step_seq",
            legacy.step_seq,
            candidate.step_seq,
            ts_exchange);
    }

    if (rules.state.strict_ts_exchange) {
        CompareStrictField<uint64_t>(
            out,
            ReplayMismatchDomain::State,
            "state.ts_exchange",
            legacy.ts_exchange,
            candidate.ts_exchange,
            ts_exchange);
    }

    if (rules.state.strict_progressed) {
        CompareStrictField<bool>(
            out,
            ReplayMismatchDomain::Orchestration,
            "progress.progressed",
            legacy.progress.progressed,
            candidate.progress.progressed,
            ts_exchange);
    }

    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.perp_wallet_balance",
        legacy.account.perp_wallet_balance,
        candidate.account.perp_wallet_balance,
        rules.state.tolerance.wallet_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.spot_wallet_balance",
        legacy.account.spot_wallet_balance,
        candidate.account.spot_wallet_balance,
        rules.state.tolerance.wallet_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.total_cash_balance",
        legacy.account.total_cash_balance,
        candidate.account.total_cash_balance,
        rules.state.tolerance.cash_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.total_ledger_value",
        legacy.account.total_ledger_value,
        candidate.account.total_ledger_value,
        rules.state.tolerance.ledger_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.total_ledger_value_base",
        legacy.account.total_ledger_value_base,
        candidate.account.total_ledger_value_base,
        rules.state.tolerance.ledger_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.total_ledger_value_conservative",
        legacy.account.total_ledger_value_conservative,
        candidate.account.total_ledger_value_conservative,
        rules.state.tolerance.ledger_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "account.total_ledger_value_optimistic",
        legacy.account.total_ledger_value_optimistic,
        candidate.account.total_ledger_value_optimistic,
        rules.state.tolerance.ledger_abs,
        ts_exchange);

    CompareStrictField<uint64_t>(
        out,
        ReplayMismatchDomain::State,
        "order.open_order_count",
        legacy.order.open_order_count,
        candidate.order.open_order_count,
        ts_exchange);
    CompareStrictField<uint64_t>(
        out,
        ReplayMismatchDomain::State,
        "order.rejection_count",
        legacy.order.rejection_count,
        candidate.order.rejection_count,
        ts_exchange);
    CompareStrictField<uint64_t>(
        out,
        ReplayMismatchDomain::State,
        "order.liquidation_count",
        legacy.order.liquidation_count,
        candidate.order.liquidation_count,
        ts_exchange);

    CompareStrictField<uint64_t>(
        out,
        ReplayMismatchDomain::State,
        "position.position_count",
        legacy.position.position_count,
        candidate.position.position_count,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "position.gross_position_notional",
        legacy.position.gross_position_notional,
        candidate.position.gross_position_notional,
        rules.state.tolerance.amount_abs,
        ts_exchange);
    CompareFloatField(
        out,
        ReplayMismatchDomain::State,
        "position.net_position_notional",
        legacy.position.net_position_notional,
        candidate.position.net_position_notional,
        rules.state.tolerance.amount_abs,
        ts_exchange);
}

void CompareEventPair(
    ReplayStepCompareResult& out,
    const ReplayEventSummary& legacy,
    const ReplayEventSummary& candidate,
    const StepCompareRules& rules,
    size_t event_index)
{
    const uint64_t ts_exchange = legacy.ts_exchange;
    const uint64_t event_seq = legacy.event_seq;
    const std::string prefix = "event[" + std::to_string(event_index) + "]";

    CompareStrictField<std::string>(
        out,
        ReplayMismatchDomain::Event,
        prefix + ".type",
        ToString(legacy.type),
        ToString(candidate.type),
        ts_exchange,
        event_seq);

    if (rules.event.strict_event_seq) {
        CompareStrictField<uint64_t>(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".event_seq",
            legacy.event_seq,
            candidate.event_seq,
            ts_exchange,
            event_seq);
    }

    CompareStrictField<uint64_t>(
        out,
        ReplayMismatchDomain::Event,
        prefix + ".ts_exchange",
        legacy.ts_exchange,
        candidate.ts_exchange,
        ts_exchange,
        event_seq);

    if (rules.event.strict_symbol) {
        CompareStrictField<std::string>(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".symbol",
            legacy.symbol,
            candidate.symbol,
            ts_exchange,
            event_seq);
    }

    CompareStrictField<std::string>(
        out,
        ReplayMismatchDomain::Event,
        prefix + ".event_id",
        legacy.event_id,
        candidate.event_id,
        ts_exchange,
        event_seq);

    switch (legacy.type) {
    case ReplayEventType::Fill:
        CompareFloatField(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".quantity",
            legacy.quantity,
            candidate.quantity,
            rules.event.tolerance.quantity_abs,
            ts_exchange,
            event_seq);
        CompareFloatField(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".price",
            legacy.price,
            candidate.price,
            rules.event.tolerance.price_abs,
            ts_exchange,
            event_seq);
        break;
    case ReplayEventType::Funding:
        CompareFloatField(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".amount",
            legacy.amount,
            candidate.amount,
            rules.event.tolerance.amount_abs,
            ts_exchange,
            event_seq);
        CompareFloatField(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".price",
            legacy.price,
            candidate.price,
            rules.event.tolerance.price_abs,
            ts_exchange,
            event_seq);
        break;
    case ReplayEventType::Rejection:
    case ReplayEventType::Liquidation:
        CompareStrictField<int32_t>(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".reject_code",
            legacy.reject_code,
            candidate.reject_code,
            ts_exchange,
            event_seq);
        CompareFloatField(
            out,
            ReplayMismatchDomain::Event,
            prefix + ".amount",
            legacy.amount,
            candidate.amount,
            rules.event.tolerance.amount_abs,
            ts_exchange,
            event_seq);
        break;
    case ReplayEventType::AsyncAck: {
        const ReplayMismatchDomain timeline_domain = DomainForEventField(legacy.type, true);
        CompareStrictField<uint64_t>(
            out,
            timeline_domain,
            prefix + ".request_id",
            legacy.request_id,
            candidate.request_id,
            ts_exchange,
            event_seq);
        CompareStrictField<std::string>(
            out,
            timeline_domain,
            prefix + ".status",
            legacy.status,
            candidate.status,
            ts_exchange,
            event_seq);
        if (rules.event.strict_async_ack_timeline) {
            CompareStrictField<uint64_t>(
                out,
                timeline_domain,
                prefix + ".submitted_step",
                legacy.submitted_step,
                candidate.submitted_step,
                ts_exchange,
                event_seq);
            CompareStrictField<uint64_t>(
                out,
                timeline_domain,
                prefix + ".due_step",
                legacy.due_step,
                candidate.due_step,
                ts_exchange,
                event_seq);
            CompareStrictField<uint64_t>(
                out,
                timeline_domain,
                prefix + ".resolved_step",
                legacy.resolved_step,
                candidate.resolved_step,
                ts_exchange,
                event_seq);
        }
        break;
    }
    }
}

void CompareEvents(
    ReplayStepCompareResult& out,
    const StepEventComparePayload& legacy,
    const StepEventComparePayload& candidate,
    const StepCompareRules& rules,
    uint64_t ts_exchange)
{
    std::vector<const ReplayEventSummary*> filtered_legacy;
    std::vector<const ReplayEventSummary*> filtered_candidate;
    filtered_legacy.reserve(legacy.events.size());
    filtered_candidate.reserve(candidate.events.size());

    for (const auto& event : legacy.events) {
        if (IsEventTypeEnabled(event.type, rules.event)) {
            filtered_legacy.push_back(&event);
        }
    }
    for (const auto& event : candidate.events) {
        if (IsEventTypeEnabled(event.type, rules.event)) {
            filtered_candidate.push_back(&event);
        }
    }

    if (rules.event.strict_event_ordering) {
        for (size_t i = 1; i < filtered_legacy.size(); ++i) {
            if (filtered_legacy[i]->event_seq < filtered_legacy[i - 1]->event_seq) {
                AddMismatch(
                    out,
                    ReplayMismatchDomain::Event,
                    "event.ordering.legacy",
                    ToString(filtered_legacy[i - 1]->event_seq) + "->" + ToString(filtered_legacy[i]->event_seq),
                    "ascending",
                    "legacy event sequence is not monotonic",
                    ts_exchange,
                    filtered_legacy[i]->event_seq);
                break;
            }
        }
        for (size_t i = 1; i < filtered_candidate.size(); ++i) {
            if (filtered_candidate[i]->event_seq < filtered_candidate[i - 1]->event_seq) {
                AddMismatch(
                    out,
                    ReplayMismatchDomain::Event,
                    "event.ordering.candidate",
                    ToString(filtered_candidate[i - 1]->event_seq) + "->" + ToString(filtered_candidate[i]->event_seq),
                    "ascending",
                    "candidate event sequence is not monotonic",
                    ts_exchange,
                    filtered_candidate[i]->event_seq);
                break;
            }
        }
    }

    if (filtered_legacy.size() != filtered_candidate.size()) {
        AddMismatch(
            out,
            ReplayMismatchDomain::Event,
            "event.count",
            ToString(static_cast<uint64_t>(filtered_legacy.size())),
            ToString(static_cast<uint64_t>(filtered_candidate.size())),
            "event count mismatch",
            ts_exchange);
    }

    const size_t common_count = std::min(filtered_legacy.size(), filtered_candidate.size());
    for (size_t i = 0; i < common_count; ++i) {
        CompareEventPair(out, *filtered_legacy[i], *filtered_candidate[i], rules, i);
    }
}

} // namespace

StepCompareModel::StepCompareModel(StepCompareRules rules)
    : rules_(std::move(rules))
{
}

ReplayStepCompareResult StepCompareModel::CompareStep(
    uint64_t step_index,
    const StepComparePayload& legacy,
    const StepComparePayload& candidate) const
{
    ReplayStepCompareResult out{};
    out.step_index = step_index;
    out.ts_exchange = legacy.state.ts_exchange;
    out.status = ReplayCompareStatus::Success;
    out.compared = true;
    out.matched = true;

    const ReplayCompareStatus legacy_status = legacy.state.progress.status;
    const ReplayCompareStatus candidate_status = candidate.state.progress.status;
    const bool legacy_fallback = legacy.state.progress.fallback_to_legacy ||
        legacy_status == ReplayCompareStatus::Fallback;
    const bool candidate_fallback = candidate.state.progress.fallback_to_legacy ||
        candidate_status == ReplayCompareStatus::Fallback;

    if (legacy_status == ReplayCompareStatus::Unsupported ||
        candidate_status == ReplayCompareStatus::Unsupported) {
        out.status = ReplayCompareStatus::Unsupported;
        out.compared = false;
        out.matched = true;
        out.note = "step compare unsupported";
        return out;
    }

    if (legacy_fallback && candidate_fallback) {
        out.status = ReplayCompareStatus::Fallback;
        out.compared = false;
        out.matched = true;
        out.fallback_to_legacy = true;
        out.note = "both sides fallback to legacy";
        return out;
    }

    if (legacy_fallback != candidate_fallback) {
        AddMismatch(
            out,
            ReplayMismatchDomain::Orchestration,
            "progress.fallback_to_legacy",
            ToString(legacy_fallback),
            ToString(candidate_fallback),
            "fallback mismatch",
            out.ts_exchange);
    }

    if (legacy_status != candidate_status) {
        AddMismatch(
            out,
            ReplayMismatchDomain::Orchestration,
            "progress.status",
            ToString(legacy_status),
            ToString(candidate_status),
            "status mismatch",
            out.ts_exchange);
    }

    if (rules_.state.enabled) {
        CompareState(out, legacy.state, candidate.state, rules_);
    }
    if (rules_.event.enabled) {
        CompareEvents(out, legacy.event, candidate.event, rules_, out.ts_exchange);
    }

    for (auto& mismatch : out.mismatches) {
        mismatch.step_index = step_index;
        if (mismatch.ts_exchange == 0) {
            mismatch.ts_exchange = out.ts_exchange;
        }
    }

    if (!out.mismatches.empty()) {
        out.status = ReplayCompareStatus::Failed;
        out.matched = false;
    } else {
        out.status = ReplayCompareStatus::Success;
        out.matched = true;
    }

    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
