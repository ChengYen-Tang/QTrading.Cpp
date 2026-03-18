#include "Exchanges/BinanceSimulator/Diagnostics/Compare/BinanceCompareBridge.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

std::string AckStatusToString(BinanceExchange::AsyncOrderAck::Status status)
{
    switch (status) {
    case BinanceExchange::AsyncOrderAck::Status::Pending:
        return "Pending";
    case BinanceExchange::AsyncOrderAck::Status::Accepted:
        return "Accepted";
    case BinanceExchange::AsyncOrderAck::Status::Rejected:
        return "Rejected";
    }
    return "Unknown";
}

} // namespace

StepStateComparePayload BinanceCompareBridge::FromStepCompareSnapshot(
    const BinanceExchange::StepCompareSnapshot& snapshot,
    ReplayCompareStatus status,
    bool fallback_to_legacy)
{
    StepStateComparePayload payload{};
    payload.step_seq = snapshot.step_seq;
    payload.ts_exchange = snapshot.ts_exchange;
    payload.progress.progressed = snapshot.progressed;
    payload.progress.fallback_to_legacy = fallback_to_legacy;
    payload.progress.status = status;
    payload.account.perp_wallet_balance = snapshot.perp_wallet_balance;
    payload.account.spot_wallet_balance = snapshot.spot_wallet_balance;
    payload.account.total_cash_balance = snapshot.total_cash_balance;
    payload.account.total_ledger_value = snapshot.total_cash_balance;
    payload.account.total_ledger_value_base = snapshot.total_cash_balance;
    payload.account.total_ledger_value_conservative = snapshot.total_cash_balance;
    payload.account.total_ledger_value_optimistic = snapshot.total_cash_balance;
    payload.order.open_order_count = snapshot.open_order_count;
    payload.position.position_count = snapshot.position_count;
    return payload;
}

ReplayEventSummary BinanceCompareBridge::FromAsyncOrderAck(
    const BinanceExchange::AsyncOrderAck& ack,
    uint64_t ts_exchange)
{
    ReplayEventSummary event{};
    event.type = ReplayEventType::AsyncAck;
    event.event_seq = ack.request_id;
    event.ts_exchange = ts_exchange;
    event.symbol = ack.symbol;
    event.event_id = ack.client_order_id;
    event.quantity = ack.quantity;
    event.price = ack.price;
    event.reject_code = static_cast<int32_t>(ack.reject_code);
    event.status = AckStatusToString(ack.status);
    event.request_id = ack.request_id;
    event.submitted_step = ack.submitted_step;
    event.due_step = ack.due_step;
    event.resolved_step = ack.resolved_step;
    return event;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
