#include "Exchanges/BinanceSimulator/Diagnostics/Compare/V2ReplayScenarioPack.hpp"

#include <utility>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

constexpr const char* kDatasetId = "v2-legacy-replay-pack-20260319";
constexpr uint64_t kBaseTs = 1'700'000'000'000ULL;

StepComparePayload BuildStep(
    uint64_t step_seq,
    uint64_t ts_exchange,
    double perp_wallet_balance,
    double spot_wallet_balance,
    double total_cash_balance,
    uint64_t open_order_count,
    uint64_t position_count,
    uint64_t rejection_count,
    uint64_t liquidation_count,
    double gross_position_notional,
    double net_position_notional)
{
    StepComparePayload payload{};
    payload.state.step_seq = step_seq;
    payload.state.ts_exchange = ts_exchange;
    payload.state.progress.progressed = true;
    payload.state.progress.status = ReplayCompareStatus::Success;
    payload.state.account.perp_wallet_balance = perp_wallet_balance;
    payload.state.account.spot_wallet_balance = spot_wallet_balance;
    payload.state.account.total_cash_balance = total_cash_balance;
    payload.state.account.total_ledger_value = total_cash_balance;
    payload.state.account.total_ledger_value_base = total_cash_balance;
    payload.state.account.total_ledger_value_conservative = total_cash_balance;
    payload.state.account.total_ledger_value_optimistic = total_cash_balance;
    payload.state.order.open_order_count = open_order_count;
    payload.state.order.rejection_count = rejection_count;
    payload.state.order.liquidation_count = liquidation_count;
    payload.state.position.position_count = position_count;
    payload.state.position.gross_position_notional = gross_position_notional;
    payload.state.position.net_position_notional = net_position_notional;
    return payload;
}

ReplayEventSummary BuildEvent(
    ReplayEventType type,
    uint64_t event_seq,
    uint64_t ts_exchange,
    std::string symbol,
    std::string event_id)
{
    ReplayEventSummary event{};
    event.type = type;
    event.event_seq = event_seq;
    event.ts_exchange = ts_exchange;
    event.ts_local = ts_exchange + 7;
    event.symbol = std::move(symbol);
    event.event_id = std::move(event_id);
    event.quantity = 1.0;
    event.price = 100.0;
    event.amount = 0.0;
    event.reject_code = 0;
    event.status = "Accepted";
    event.request_id = event_seq;
    event.submitted_step = 0;
    event.due_step = 0;
    event.resolved_step = 0;
    return event;
}

LegacyLogCompareRow BuildRow(
    uint64_t arrival_index,
    uint64_t batch_boundary,
    int32_t module_id,
    std::string module_name,
    uint64_t ts_exchange,
    uint64_t step_seq,
    uint64_t event_seq,
    std::string symbol,
    LegacyLogRowKind row_kind,
    std::vector<LegacyLogPayloadField> payload_fields)
{
    LegacyLogCompareRow row{};
    row.arrival_index = arrival_index;
    row.batch_boundary = batch_boundary;
    row.module_id = module_id;
    row.module_name = std::move(module_name);
    row.ts_exchange = ts_exchange;
    row.ts_local = ts_exchange + 7;
    row.run_id = 1;
    row.step_seq = step_seq;
    row.event_seq = event_seq;
    row.symbol = std::move(symbol);
    row.row_kind = row_kind;
    row.payload_fields = std::move(payload_fields);
    return row;
}

ReplayCompareScenarioData BuildSingleSymbolScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.single-symbol";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 60'000;
    scenario.scenario.expected_steps = 2;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 1002.5, 0.0, 1002.5, 0, 1, 0, 0, 100.5, 100.5);
    step1.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 1, kBaseTs, "BTCUSDT", "fill-open-1"));
    step2.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 2, kBaseTs + 60'000, "BTCUSDT", "fill-close-2"));

    scenario.legacy_steps = { step1, step2 };
    scenario.candidate_steps = scenario.legacy_steps;

    scenario.legacy_rows = {
        BuildRow(0, 0, 10, "Account", kBaseTs, 1, 0, "BTCUSDT", LegacyLogRowKind::Snapshot, {{"wallet", "1000.0"}}),
        BuildRow(1, 1, 11, "OrderEvent", kBaseTs, 1, 1, "BTCUSDT", LegacyLogRowKind::Event, {{"event", "fill-open-1"}}),
        BuildRow(2, 0, 10, "Account", kBaseTs + 60'000, 2, 0, "BTCUSDT", LegacyLogRowKind::Snapshot, {{"wallet", "1002.5"}}),
        BuildRow(3, 1, 11, "OrderEvent", kBaseTs + 60'000, 2, 2, "BTCUSDT", LegacyLogRowKind::Event, {{"event", "fill-close-2"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "single-symbol row[0] account snapshot",
        "single-symbol row[2] account snapshot"
    };
    return scenario;
}

ReplayCompareScenarioData BuildBasisStressScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.basis-stress";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 180'000;
    scenario.scenario.expected_steps = 4;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 2, 2, 0, 0, 200.0, 0.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 997.25, 0.0, 997.25, 2, 2, 0, 0, 240.0, 120.0);
    auto step3 = BuildStep(3, kBaseTs + 120'000, 1002.10, 0.0, 1002.10, 1, 1, 0, 0, 180.0, 90.0);
    auto step4 = BuildStep(4, kBaseTs + 180'000, 1005.50, 0.0, 1005.50, 0, 0, 0, 0, 0.0, 0.0);

    auto open = BuildEvent(ReplayEventType::Fill, 501, kBaseTs, "BTCUSDT", "basis-open");
    open.quantity = 2.0;
    open.price = 100.0;
    auto adverse = BuildEvent(ReplayEventType::Fill, 502, kBaseTs + 60'000, "BTCUSDT", "basis-adverse");
    adverse.quantity = 2.0;
    adverse.price = 98.25;
    auto recover = BuildEvent(ReplayEventType::Fill, 503, kBaseTs + 120'000, "BTCUSDT", "basis-recover");
    recover.quantity = 1.0;
    recover.price = 102.10;
    auto flatten = BuildEvent(ReplayEventType::Fill, 504, kBaseTs + 180'000, "BTCUSDT", "basis-flatten");
    flatten.quantity = 1.0;
    flatten.price = 105.50;

    step1.event.events.push_back(std::move(open));
    step2.event.events.push_back(std::move(adverse));
    step3.event.events.push_back(std::move(recover));
    step4.event.events.push_back(std::move(flatten));

    scenario.legacy_steps = { step1, step2, step3, step4 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 60, "BasisEvent", kBaseTs, 1, 501, "BTCUSDT", LegacyLogRowKind::Event, {{"basis_spread_bps", "12.0"}}),
        BuildRow(1, 0, 60, "BasisEvent", kBaseTs + 60'000, 2, 502, "BTCUSDT", LegacyLogRowKind::Event, {{"basis_spread_bps", "-45.0"}}),
        BuildRow(2, 0, 60, "BasisEvent", kBaseTs + 120'000, 3, 503, "BTCUSDT", LegacyLogRowKind::Event, {{"basis_spread_bps", "18.0"}}),
        BuildRow(3, 1, 60, "BasisEvent", kBaseTs + 180'000, 4, 504, "BTCUSDT", LegacyLogRowKind::Event, {{"basis_spread_bps", "0.0"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "basis-stress row[1] adverse spread",
        "basis-stress row[3] flattened"
    };
    return scenario;
}

ReplayCompareScenarioData BuildMixedSpotPerpScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.mixed-spot-perp";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 120'000;
    scenario.scenario.expected_steps = 3;

    auto step1 = BuildStep(1, kBaseTs, 600.0, 400.0, 1000.0, 2, 2, 0, 0, 200.0, 20.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 590.0, 415.0, 1005.0, 2, 2, 0, 0, 220.0, 15.0);
    auto step3 = BuildStep(3, kBaseTs + 120'000, 605.0, 405.0, 1010.0, 1, 1, 0, 0, 150.0, 10.0);

    auto spot_fill = BuildEvent(ReplayEventType::Fill, 601, kBaseTs, "BTCUSDT-SPOT", "spot-fill-601");
    spot_fill.quantity = 0.4;
    spot_fill.price = 25000.0;
    auto perp_fill = BuildEvent(ReplayEventType::Fill, 602, kBaseTs + 60'000, "BTCUSDT-PERP", "perp-fill-602");
    perp_fill.quantity = 0.4;
    perp_fill.price = 25010.0;
    auto rebalance = BuildEvent(ReplayEventType::Fill, 603, kBaseTs + 120'000, "BTCUSDT-PERP", "rebalance-603");
    rebalance.quantity = 0.2;
    rebalance.price = 25030.0;

    step1.event.events.push_back(std::move(spot_fill));
    step2.event.events.push_back(std::move(perp_fill));
    step3.event.events.push_back(std::move(rebalance));

    scenario.legacy_steps = { step1, step2, step3 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 61, "SpotOrderEvent", kBaseTs, 1, 601, "BTCUSDT-SPOT", LegacyLogRowKind::Event, {{"market", "spot"}}),
        BuildRow(1, 0, 62, "PerpOrderEvent", kBaseTs + 60'000, 2, 602, "BTCUSDT-PERP", LegacyLogRowKind::Event, {{"market", "perp"}}),
        BuildRow(2, 1, 62, "PerpOrderEvent", kBaseTs + 120'000, 3, 603, "BTCUSDT-PERP", LegacyLogRowKind::Event, {{"market", "perp"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "mixed-spot-perp row[0] spot fill",
        "mixed-spot-perp row[1] perp fill"
    };
    return scenario;
}

ReplayCompareScenarioData BuildFundingReferenceEdgeScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.funding-reference-edge";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 120'000;
    scenario.scenario.expected_steps = 3;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 999.88, 0.0, 999.88, 1, 1, 0, 0, 100.0, 100.0);
    auto step3 = BuildStep(3, kBaseTs + 120'000, 1000.11, 0.0, 1000.11, 1, 1, 0, 0, 100.5, 100.5);

    auto funding_mark = BuildEvent(
        ReplayEventType::Funding, 11, kBaseTs + 60'000, "BTCUSDT", "funding-mark");
    funding_mark.amount = -0.12;
    funding_mark.price = 100.0;
    auto funding_index = BuildEvent(
        ReplayEventType::Funding, 12, kBaseTs + 120'000, "BTCUSDT", "funding-index-fallback");
    funding_index.amount = 0.23;
    funding_index.price = 100.5;

    step2.event.events.push_back(std::move(funding_mark));
    step3.event.events.push_back(std::move(funding_index));

    scenario.legacy_steps = { step1, step2, step3 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 30, "FundingEvent", kBaseTs + 60'000, 2, 11, "BTCUSDT", LegacyLogRowKind::Event, {{"funding", "-0.12"}, {"mark", "100.0"}, {"reference_source", "mark"}}),
        BuildRow(1, 0, 30, "FundingEvent", kBaseTs + 120'000, 3, 12, "BTCUSDT", LegacyLogRowKind::Event, {{"funding", "0.23"}, {"mark", "100.5"}, {"reference_source", "index-fallback"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "funding-reference-edge row[0] mark source",
        "funding-reference-edge row[1] index fallback source"
    };
    return scenario;
}

ReplayCompareScenarioData BuildAsyncAckLatencyScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.async-ack-latency";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 60'000;
    scenario.scenario.expected_steps = 2;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 0, 0, 0, 0.0, 0.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 1000.0, 0.0, 1000.0, 0, 1, 0, 0, 100.0, 100.0);
    auto ack_pending = BuildEvent(
        ReplayEventType::AsyncAck, 101, kBaseTs, "BTCUSDT", "cid-ack-101");
    ack_pending.status = "Pending";
    ack_pending.request_id = 101;
    ack_pending.submitted_step = 1;
    ack_pending.due_step = 2;
    ack_pending.resolved_step = 0;
    auto ack_accepted = ack_pending;
    ack_accepted.event_seq = 102;
    ack_accepted.ts_exchange = kBaseTs + 60'000;
    ack_accepted.status = "Accepted";
    ack_accepted.resolved_step = 2;
    step1.event.events.push_back(std::move(ack_pending));
    step2.event.events.push_back(std::move(ack_accepted));

    scenario.legacy_steps = { step1, step2 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 40, "OrderEvent", kBaseTs, 1, 101, "BTCUSDT", LegacyLogRowKind::Event, {{"ack", "Pending"}, {"due_step", "2"}}),
        BuildRow(1, 0, 40, "OrderEvent", kBaseTs + 60'000, 2, 102, "BTCUSDT", LegacyLogRowKind::Event, {{"ack", "Accepted"}, {"resolved_step", "2"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "async-ack row[0] pending",
        "async-ack row[1] accepted"
    };
    return scenario;
}

ReplayCompareScenarioData BuildRejectionLiquidationScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.rejection-liquidation";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 120'000;
    scenario.scenario.expected_steps = 3;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 1000.0, 0.0, 1000.0, 1, 1, 1, 0, 100.0, 100.0);
    auto step3 = BuildStep(3, kBaseTs + 120'000, 980.0, 0.0, 980.0, 0, 0, 1, 1, 0.0, 0.0);

    auto rejection = BuildEvent(
        ReplayEventType::Rejection, 201, kBaseTs + 60'000, "BTCUSDT", "reject-201");
    rejection.reject_code = -2010;
    rejection.amount = 0.0;
    auto liquidation = BuildEvent(
        ReplayEventType::Liquidation, 301, kBaseTs + 120'000, "BTCUSDT", "liq-301");
    liquidation.amount = -20.0;
    step2.event.events.push_back(std::move(rejection));
    step3.event.events.push_back(std::move(liquidation));

    scenario.legacy_steps = { step1, step2, step3 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 50, "OrderEvent", kBaseTs + 60'000, 2, 201, "BTCUSDT", LegacyLogRowKind::Event, {{"reject_code", "-2010"}}),
        BuildRow(1, 0, 51, "AccountEvent", kBaseTs + 120'000, 3, 301, "BTCUSDT", LegacyLogRowKind::Event, {{"liquidation", "-20.0"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "rejection-liquidation row[0] rejection event",
        "rejection-liquidation row[1] liquidation event"
    };
    return scenario;
}

} // namespace

std::vector<ReplayCompareScenarioData> V2ReplayScenarioPack::BuildCoreScenarioPack()
{
    std::vector<ReplayCompareScenarioData> out;
    out.reserve(6);
    out.push_back(BuildSingleSymbolScenario());
    out.push_back(BuildBasisStressScenario());
    out.push_back(BuildMixedSpotPerpScenario());
    out.push_back(BuildFundingReferenceEdgeScenario());
    out.push_back(BuildAsyncAckLatencyScenario());
    out.push_back(BuildRejectionLiquidationScenario());
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
