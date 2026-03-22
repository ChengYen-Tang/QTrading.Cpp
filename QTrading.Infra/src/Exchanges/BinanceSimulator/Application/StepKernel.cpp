#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"

#include <algorithm>
#include <utility>

#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/ChannelPublisher.hpp"
#include "Exchanges/BinanceSimulator/Output/StepObservableContext.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
namespace {

// Computes replay progress using current cursors without rebuilding payloads.
// Kept in O(symbol_count) and allocation-free for per-step usage.
double compute_progress_pct(const State::StepKernelState& step_state)
{
    if (step_state.market_data.empty()) {
        return 0.0;
    }
    double min_ratio = 1.0;
    bool has_symbol = false;
    const size_t count = std::min(step_state.replay_cursor.size(), step_state.market_data.size());
    for (size_t i = 0; i < count; ++i) {
        const size_t total = step_state.market_data[i].get_klines_count();
        if (total == 0) {
            continue;
        }
        has_symbol = true;
        const size_t progressed = std::min(step_state.replay_cursor[i], total);
        const double ratio = static_cast<double>(progressed) / static_cast<double>(total);
        if (ratio < min_ratio) {
            min_ratio = ratio;
        }
    }
    if (!has_symbol) {
        return 0.0;
    }
    return std::clamp(min_ratio, 0.0, 1.0) * 100.0;
}

// Writes the minimal read-model state consumed by FillStatusSnapshot().
// Hot-path note: updates are in-place and only touch fields needed by Phase 3.
void update_snapshot_state(
    State::SnapshotState& snapshot_state,
    const State::StepKernelState& step_state,
    const Output::StepObservableContext& observable_ctx)
{
    snapshot_state.ts_exchange = observable_ctx.ts_exchange;
    snapshot_state.step_seq = observable_ctx.step_seq;
    snapshot_state.progress_pct = compute_progress_pct(step_state);
    snapshot_state.last_market_payload = observable_ctx.market_payload;
    if (!observable_ctx.market_payload) {
        return;
    }
    const auto& payload = *observable_ctx.market_payload;
    const size_t count = std::min(
        payload.trade_klines_by_id.size(),
        snapshot_state.last_trade_price_by_symbol.size());
    for (size_t i = 0; i < count; ++i) {
        if (!payload.trade_klines_by_id[i].has_value()) {
            continue;
        }
        snapshot_state.last_trade_price_by_symbol[i] = payload.trade_klines_by_id[i]->ClosePrice;
        snapshot_state.has_last_trade_price_by_symbol[i] = 1;
    }
}

} // namespace

StepKernel::StepKernel(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

bool StepKernel::run_step() const
{
    // Main hot path for phase-1/2/3 skeleton:
    // 1) termination check, 2) replay frame build, 3) context publish/update.
    auto& runtime_state = *exchange_.runtime_state_;
    auto& step_state = *exchange_.step_kernel_state_;
    auto& snapshot_state = *exchange_.snapshot_state_;

    if (TerminationPolicy::IsReplayExhausted(step_state)) {
        TerminationPolicy::CloseChannels(exchange_, step_state);
        return false;
    }

    auto frame = MarketReplayKernel::Next(step_state);
    if (!frame.has_next) {
        TerminationPolicy::CloseChannels(exchange_, step_state);
        return false;
    }

    ++step_state.step_seq;
    OrderCommandKernel(exchange_).FlushDeferredForStep(step_state.step_seq);
    runtime_state.last_status_snapshot.ts_exchange = frame.ts_exchange;

    Output::StepObservableContext observable_ctx{};
    observable_ctx.ts_exchange = frame.ts_exchange;
    observable_ctx.step_seq = step_state.step_seq;
    observable_ctx.replay_exhausted = false;
    observable_ctx.market_payload = std::move(frame.market_payload);
    update_snapshot_state(snapshot_state, step_state, observable_ctx);
    Output::ChannelPublisher::PublishStep(exchange_, observable_ctx);
    step_state.channels_closed = false;
    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
