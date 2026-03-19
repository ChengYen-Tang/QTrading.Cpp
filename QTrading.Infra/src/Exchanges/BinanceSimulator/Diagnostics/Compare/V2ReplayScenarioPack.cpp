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

ReplayCompareScenarioData BuildDualSymbolHolesScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.dual-symbol-holes";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 120'000;
    scenario.scenario.expected_steps = 3;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step3 = BuildStep(3, kBaseTs + 120'000, 1001.1, 0.0, 1001.1, 2, 2, 0, 0, 200.0, 0.0);

    step1.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 1, kBaseTs, "BTCUSDT", "btc-fill-open"));
    step2.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 2, kBaseTs + 60'000, "ETHUSDT", "eth-fill-open"));
    step3.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 3, kBaseTs + 120'000, "BTCUSDT", "btc-adjust"));
    step3.event.events.push_back(BuildEvent(
        ReplayEventType::Fill, 4, kBaseTs + 120'000, "ETHUSDT", "eth-adjust"));

    scenario.legacy_steps = { step1, step2, step3 };
    scenario.candidate_steps = scenario.legacy_steps;

    scenario.legacy_rows = {
        BuildRow(0, 0, 20, "MarketEvent", kBaseTs, 1, 1, "BTCUSDT", LegacyLogRowKind::Event, {{"holes", "ETH-missing"}}),
        BuildRow(1, 0, 20, "MarketEvent", kBaseTs + 60'000, 2, 2, "ETHUSDT", LegacyLogRowKind::Event, {{"holes", "BTC-missing"}}),
        BuildRow(2, 0, 20, "MarketEvent", kBaseTs + 120'000, 3, 3, "BTCUSDT", LegacyLogRowKind::Event, {{"holes", "none"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "dual-symbol-holes row[0] missing ETH",
        "dual-symbol-holes row[1] missing BTC"
    };
    return scenario;
}

ReplayCompareScenarioData BuildFundingScenario()
{
    ReplayCompareScenarioData scenario{};
    scenario.scenario.name = "v2-vs-legacy.funding";
    scenario.scenario.dataset_id = kDatasetId;
    scenario.scenario.start_ts_exchange = kBaseTs;
    scenario.scenario.end_ts_exchange = kBaseTs + 60'000;
    scenario.scenario.expected_steps = 2;

    auto step1 = BuildStep(1, kBaseTs, 1000.0, 0.0, 1000.0, 1, 1, 0, 0, 100.0, 100.0);
    auto step2 = BuildStep(2, kBaseTs + 60'000, 999.88, 0.0, 999.88, 1, 1, 0, 0, 100.0, 100.0);
    auto funding = BuildEvent(
        ReplayEventType::Funding, 11, kBaseTs + 60'000, "BTCUSDT", "funding-11");
    funding.amount = -0.12;
    funding.price = 100.0;
    step2.event.events.push_back(std::move(funding));

    scenario.legacy_steps = { step1, step2 };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_rows = {
        BuildRow(0, 0, 30, "FundingEvent", kBaseTs + 60'000, 2, 11, "BTCUSDT", LegacyLogRowKind::Event, {{"funding", "-0.12"}, {"mark", "100.0"}}),
    };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = {
        "funding row[0] ts_exchange aligned with funding timestamp"
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
    out.reserve(5);
    out.push_back(BuildSingleSymbolScenario());
    out.push_back(BuildDualSymbolHolesScenario());
    out.push_back(BuildFundingScenario());
    out.push_back(BuildAsyncAckLatencyScenario());
    out.push_back(BuildRejectionLiquidationScenario());
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
