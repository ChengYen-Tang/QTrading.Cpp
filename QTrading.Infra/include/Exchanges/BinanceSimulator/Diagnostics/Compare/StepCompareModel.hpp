#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

struct StepProgressSummary {
    bool progressed{ false };
    bool fallback_to_legacy{ false };
    ReplayCompareStatus status{ ReplayCompareStatus::Success };
};

struct AccountStateSummary {
    double perp_wallet_balance{ 0.0 };
    double spot_wallet_balance{ 0.0 };
    double total_cash_balance{ 0.0 };
    double total_ledger_value{ 0.0 };
    double total_ledger_value_base{ 0.0 };
    double total_ledger_value_conservative{ 0.0 };
    double total_ledger_value_optimistic{ 0.0 };
};

struct OrderStateSummary {
    uint64_t open_order_count{ 0 };
    uint64_t rejection_count{ 0 };
    uint64_t liquidation_count{ 0 };
};

struct PositionStateSummary {
    uint64_t position_count{ 0 };
    double gross_position_notional{ 0.0 };
    double net_position_notional{ 0.0 };
};

struct StepStateComparePayload {
    uint64_t step_seq{ 0 };
    uint64_t ts_exchange{ 0 };
    StepProgressSummary progress{};
    AccountStateSummary account{};
    OrderStateSummary order{};
    PositionStateSummary position{};
};

enum class ReplayEventType : uint8_t {
    Fill = 0,
    Funding = 1,
    Rejection = 2,
    Liquidation = 3,
    AsyncAck = 4,
};

struct ReplayEventSummary {
    ReplayEventType type{ ReplayEventType::Fill };
    uint64_t event_seq{ 0 };
    uint64_t ts_exchange{ 0 };
    uint64_t ts_local{ 0 };
    std::string symbol;
    std::string event_id;
    double quantity{ 0.0 };
    double price{ 0.0 };
    double amount{ 0.0 };
    int32_t reject_code{ 0 };
    std::string status;
    uint64_t request_id{ 0 };
    uint64_t submitted_step{ 0 };
    uint64_t due_step{ 0 };
    uint64_t resolved_step{ 0 };
};

struct StepEventComparePayload {
    std::vector<ReplayEventSummary> events;
};

struct StepComparePayload {
    StepStateComparePayload state{};
    StepEventComparePayload event{};
};

struct NumericTolerance {
    double wallet_abs{ 1e-9 };
    double cash_abs{ 1e-9 };
    double ledger_abs{ 1e-9 };
    double quantity_abs{ 1e-9 };
    double price_abs{ 1e-9 };
    double amount_abs{ 1e-9 };
};

struct StateCompareRules {
    bool enabled{ true };
    bool strict_step_seq{ true };
    bool strict_ts_exchange{ true };
    bool strict_progressed{ true };
    NumericTolerance tolerance{};
};

struct EventCompareRules {
    bool enabled{ true };
    bool compare_fill{ true };
    bool compare_funding{ true };
    bool compare_rejection_liquidation{ true };
    bool compare_async_ack_timeline{ true };
    bool strict_event_ordering{ true };
    bool strict_event_seq{ true };
    bool strict_symbol{ true };
    bool strict_async_ack_timeline{ true };
    NumericTolerance tolerance{};
};

struct StepCompareRules {
    StateCompareRules state{};
    EventCompareRules event{};
};

class StepCompareModel final {
public:
    explicit StepCompareModel(StepCompareRules rules = {});

    ReplayStepCompareResult CompareStep(
        uint64_t step_index,
        const StepComparePayload& legacy,
        const StepComparePayload& candidate) const;

private:
    StepCompareRules rules_{};
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
