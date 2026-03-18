#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/BinanceCompareBridge.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/StepCompareModel.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;
namespace BinanceSim = QTrading::Infra::Exchanges::BinanceSim;

namespace {

ReplayCompare::StepComparePayload BuildBasePayload()
{
    ReplayCompare::StepComparePayload payload{};
    payload.state.step_seq = 10;
    payload.state.ts_exchange = 1'700'000'000'000ULL;
    payload.state.progress.progressed = true;
    payload.state.progress.status = ReplayCompare::ReplayCompareStatus::Success;

    payload.state.account.perp_wallet_balance = 1000.0;
    payload.state.account.spot_wallet_balance = 400.0;
    payload.state.account.total_cash_balance = 1400.0;
    payload.state.account.total_ledger_value = 1450.0;
    payload.state.account.total_ledger_value_base = 1448.0;
    payload.state.account.total_ledger_value_conservative = 1435.0;
    payload.state.account.total_ledger_value_optimistic = 1462.0;

    payload.state.order.open_order_count = 3;
    payload.state.order.rejection_count = 1;
    payload.state.order.liquidation_count = 0;

    payload.state.position.position_count = 2;
    payload.state.position.gross_position_notional = 12500.0;
    payload.state.position.net_position_notional = 2500.0;
    return payload;
}

ReplayCompare::ReplayEventSummary BuildEvent(
    ReplayCompare::ReplayEventType type,
    uint64_t event_seq)
{
    ReplayCompare::ReplayEventSummary event{};
    event.type = type;
    event.event_seq = event_seq;
    event.ts_exchange = 1'700'000'000'000ULL;
    event.ts_local = 1'700'000'000'001ULL;
    event.symbol = "BTCUSDT";
    event.event_id = "evt-" + std::to_string(event_seq);
    event.quantity = 1.25;
    event.price = 100.5;
    event.amount = 12.34;
    event.reject_code = -2010;
    event.status = "Accepted";
    event.request_id = event_seq + 100;
    event.submitted_step = 10;
    event.due_step = 11;
    event.resolved_step = 11;
    return event;
}

std::optional<ReplayCompare::ReplayMismatch> FindMismatch(
    const ReplayCompare::ReplayStepCompareResult& result,
    const std::string& field)
{
    for (const auto& mismatch : result.mismatches) {
        if (mismatch.field == field) {
            return mismatch;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(StepCompareModelTests, BinanceBridgeMapsStepCompareSnapshotAsCompareStartPoint)
{
    BinanceSim::BinanceExchange::StepCompareSnapshot snapshot{};
    snapshot.ts_exchange = 1234567;
    snapshot.step_seq = 77;
    snapshot.progressed = true;
    snapshot.position_count = 5;
    snapshot.open_order_count = 8;
    snapshot.perp_wallet_balance = 88.5;
    snapshot.spot_wallet_balance = 11.25;
    snapshot.total_cash_balance = 99.75;

    const auto state = ReplayCompare::BinanceCompareBridge::FromStepCompareSnapshot(snapshot);

    EXPECT_EQ(state.ts_exchange, 1234567u);
    EXPECT_EQ(state.step_seq, 77u);
    EXPECT_TRUE(state.progress.progressed);
    EXPECT_EQ(state.progress.status, ReplayCompare::ReplayCompareStatus::Success);
    EXPECT_EQ(state.position.position_count, 5u);
    EXPECT_EQ(state.order.open_order_count, 8u);
    EXPECT_DOUBLE_EQ(state.account.perp_wallet_balance, 88.5);
    EXPECT_DOUBLE_EQ(state.account.spot_wallet_balance, 11.25);
    EXPECT_DOUBLE_EQ(state.account.total_cash_balance, 99.75);
}

TEST(StepCompareModelTests, BinanceBridgeMapsAsyncAckIntoTimelineEvent)
{
    BinanceSim::BinanceExchange::AsyncOrderAck ack{};
    ack.request_id = 101;
    ack.status = BinanceSim::BinanceExchange::AsyncOrderAck::Status::Rejected;
    ack.symbol = "ETHUSDT";
    ack.quantity = 2.0;
    ack.price = 2500.0;
    ack.submitted_step = 3;
    ack.due_step = 4;
    ack.resolved_step = 4;
    ack.client_order_id = "cid-101";
    ack.reject_code = ::Account::OrderRejectInfo::Code::SpotNoInventory;

    const auto event = ReplayCompare::BinanceCompareBridge::FromAsyncOrderAck(ack, 777);
    EXPECT_EQ(event.type, ReplayCompare::ReplayEventType::AsyncAck);
    EXPECT_EQ(event.event_seq, 101u);
    EXPECT_EQ(event.ts_exchange, 777u);
    EXPECT_EQ(event.symbol, "ETHUSDT");
    EXPECT_EQ(event.event_id, "cid-101");
    EXPECT_EQ(event.request_id, 101u);
    EXPECT_EQ(event.submitted_step, 3u);
    EXPECT_EQ(event.due_step, 4u);
    EXPECT_EQ(event.resolved_step, 4u);
    EXPECT_EQ(event.status, "Rejected");
}

TEST(StepCompareModelTests, ProgressRulesSupportUnsupportedFallbackAndProgressedMismatch)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.state.progress.status = ReplayCompare::ReplayCompareStatus::Unsupported;
    auto result = model.CompareStep(5, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Unsupported);
    EXPECT_FALSE(result.compared);

    legacy = BuildBasePayload();
    candidate = legacy;
    legacy.state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    legacy.state.progress.fallback_to_legacy = true;
    candidate.state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    candidate.state.progress.fallback_to_legacy = true;
    result = model.CompareStep(5, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Fallback);
    EXPECT_TRUE(result.fallback_to_legacy);

    legacy = BuildBasePayload();
    candidate = legacy;
    candidate.state.progress.progressed = false;
    result = model.CompareStep(5, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    const auto mismatch = FindMismatch(result, "progress.progressed");
    ASSERT_TRUE(mismatch.has_value());
    EXPECT_EQ(mismatch->domain, ReplayCompare::ReplayMismatchDomain::Orchestration);
}

TEST(StepCompareModelTests, StateCompareCoversAccountSummaryFields)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;
    candidate.state.account.perp_wallet_balance += 1.0;
    candidate.state.account.total_cash_balance -= 2.0;

    const auto result = model.CompareStep(8, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "account.perp_wallet_balance").has_value());
    EXPECT_TRUE(FindMismatch(result, "account.total_cash_balance").has_value());
}

TEST(StepCompareModelTests, StateCompareCoversOrderSummaryFields)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;
    candidate.state.order.open_order_count = 9;
    candidate.state.order.rejection_count = 7;
    candidate.state.order.liquidation_count = 2;

    const auto result = model.CompareStep(9, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "order.open_order_count").has_value());
    EXPECT_TRUE(FindMismatch(result, "order.rejection_count").has_value());
    EXPECT_TRUE(FindMismatch(result, "order.liquidation_count").has_value());
}

TEST(StepCompareModelTests, StateCompareCoversPositionSummaryFields)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;
    candidate.state.position.position_count = 3;
    candidate.state.position.gross_position_notional = 22222.0;
    candidate.state.position.net_position_notional = -123.0;

    const auto result = model.CompareStep(10, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "position.position_count").has_value());
    EXPECT_TRUE(FindMismatch(result, "position.gross_position_notional").has_value());
    EXPECT_TRUE(FindMismatch(result, "position.net_position_notional").has_value());
}

TEST(StepCompareModelTests, StateCompareSupportsToleranceAndStrictFields)
{
    ReplayCompare::StepCompareRules rules{};
    rules.state.tolerance.wallet_abs = 1e-3;
    rules.state.tolerance.cash_abs = 1e-3;
    rules.state.tolerance.ledger_abs = 1e-3;
    ReplayCompare::StepCompareModel model(rules);

    auto legacy = BuildBasePayload();
    auto candidate = legacy;
    candidate.state.account.perp_wallet_balance += 5e-4;
    candidate.state.account.total_cash_balance += 9e-4;
    candidate.state.account.total_ledger_value += 8e-4;

    auto result = model.CompareStep(11, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Success);

    candidate = legacy;
    candidate.state.order.open_order_count += 1;
    result = model.CompareStep(11, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "order.open_order_count").has_value());
}

TEST(StepCompareModelTests, EventCompareCoversFillAndFunding)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Fill, 1),
        BuildEvent(ReplayCompare::ReplayEventType::Funding, 2)
    };
    candidate.event.events = legacy.event.events;
    candidate.event.events[0].quantity = 9.0;
    candidate.event.events[1].amount = -88.0;

    const auto result = model.CompareStep(12, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "event[0].quantity").has_value());
    EXPECT_TRUE(FindMismatch(result, "event[1].amount").has_value());
}

TEST(StepCompareModelTests, EventCompareCoversRejectionAndLiquidation)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Rejection, 11),
        BuildEvent(ReplayCompare::ReplayEventType::Liquidation, 12)
    };
    candidate.event.events = legacy.event.events;
    candidate.event.events[0].reject_code = -2011;
    candidate.event.events[1].amount = 999.0;

    const auto result = model.CompareStep(13, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "event[0].reject_code").has_value());
    EXPECT_TRUE(FindMismatch(result, "event[1].amount").has_value());
}

TEST(StepCompareModelTests, EventCompareCoversAsyncAckTimeline)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.event.events = { BuildEvent(ReplayCompare::ReplayEventType::AsyncAck, 21) };
    candidate.event.events = legacy.event.events;
    candidate.event.events[0].resolved_step = 99;

    const auto result = model.CompareStep(14, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    const auto mismatch = FindMismatch(result, "event[0].resolved_step");
    ASSERT_TRUE(mismatch.has_value());
    EXPECT_EQ(mismatch->domain, ReplayCompare::ReplayMismatchDomain::AsyncAckTimeline);
}

TEST(StepCompareModelTests, EventCompareCoversEventOrderingRule)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Fill, 1),
        BuildEvent(ReplayCompare::ReplayEventType::Funding, 2)
    };
    candidate.event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Fill, 2),
        BuildEvent(ReplayCompare::ReplayEventType::Funding, 1)
    };

    const auto result = model.CompareStep(15, legacy, candidate);
    EXPECT_EQ(result.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(FindMismatch(result, "event.ordering.candidate").has_value());
}

TEST(StepCompareModelTests, FirstMismatchCarriesDeterministicStepAndEventLocation)
{
    ReplayCompare::StepCompareModel model;
    auto legacy = BuildBasePayload();
    auto candidate = legacy;

    legacy.event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Fill, 31),
        BuildEvent(ReplayCompare::ReplayEventType::Funding, 32)
    };
    candidate.event.events = legacy.event.events;
    candidate.event.events[1].amount = 1000.0;

    const auto result = model.CompareStep(27, legacy, candidate);
    ASSERT_FALSE(result.mismatches.empty());
    const auto& first = result.mismatches.front();
    EXPECT_EQ(first.step_index, 27u);
    EXPECT_EQ(first.event_seq, 32u);
    EXPECT_EQ(first.field, "event[1].amount");
    EXPECT_EQ(first.ts_exchange, legacy.state.ts_exchange);
}
