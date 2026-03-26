#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"
#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"
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

bool order_equal(const QTrading::dto::Order& lhs, const QTrading::dto::Order& rhs)
{
    return lhs.id == rhs.id &&
        lhs.symbol == rhs.symbol &&
        lhs.quantity == rhs.quantity &&
        lhs.price == rhs.price &&
        lhs.side == rhs.side &&
        lhs.position_side == rhs.position_side &&
        lhs.reduce_only == rhs.reduce_only &&
        lhs.closing_position_id == rhs.closing_position_id &&
        lhs.instrument_type == rhs.instrument_type &&
        lhs.client_order_id == rhs.client_order_id &&
        lhs.stp_mode == rhs.stp_mode &&
        lhs.close_position == rhs.close_position &&
        lhs.quote_order_qty == rhs.quote_order_qty &&
        lhs.one_way_reverse == rhs.one_way_reverse;
}

bool position_equal(const QTrading::dto::Position& lhs, const QTrading::dto::Position& rhs)
{
    return lhs.id == rhs.id &&
        lhs.order_id == rhs.order_id &&
        lhs.symbol == rhs.symbol &&
        lhs.quantity == rhs.quantity &&
        lhs.entry_price == rhs.entry_price &&
        lhs.is_long == rhs.is_long &&
        lhs.unrealized_pnl == rhs.unrealized_pnl &&
        lhs.notional == rhs.notional &&
        lhs.initial_margin == rhs.initial_margin &&
        lhs.maintenance_margin == rhs.maintenance_margin &&
        lhs.fee == rhs.fee &&
        lhs.leverage == rhs.leverage &&
        lhs.fee_rate == rhs.fee_rate &&
        lhs.instrument_type == rhs.instrument_type;
}

template <typename T, typename TEqual>
bool vector_equal(const std::vector<T>& lhs, const std::vector<T>& rhs, TEqual equal)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!equal(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

void publish_position_order_channels(
    BinanceExchange& exchange,
    const State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state)
{
    if (step_state.account_state_version == step_state.last_published_account_state_version) {
        return;
    }

    const auto& orders = runtime_state.orders;
    if (!step_state.has_published_orders) {
        if (!orders.empty() && exchange.get_order_channel()) {
            exchange.get_order_channel()->Send(orders);
            step_state.last_published_orders = orders;
            step_state.has_published_orders = true;
        }
    }
    else if (!vector_equal(orders, step_state.last_published_orders, order_equal)) {
        if (exchange.get_order_channel()) {
            exchange.get_order_channel()->Send(orders);
        }
        step_state.last_published_orders = orders;
    }

    const auto& positions = runtime_state.positions;
    if (!step_state.has_published_positions) {
        if (!positions.empty() && exchange.get_position_channel()) {
            exchange.get_position_channel()->Send(positions);
            step_state.last_published_positions = positions;
            step_state.has_published_positions = true;
        }
    }
    else if (!vector_equal(positions, step_state.last_published_positions, position_equal)) {
        if (exchange.get_position_channel()) {
            exchange.get_position_channel()->Send(positions);
        }
        step_state.last_published_positions = positions;
    }

    step_state.last_published_account_state_version = step_state.account_state_version;
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
    if (frame.market_payload) {
        auto& fills = step_state.match_fills_scratch;
        fills.reserve(runtime_state.orders.size());
        Domain::MatchingEngine::RunStep(runtime_state, step_state, *frame.market_payload, fills);
        if (!fills.empty()) {
            ++step_state.account_state_version;
        }
        Domain::FillSettlementEngine::Apply(runtime_state, exchange_.account_state(), fills);
    }

    Output::StepObservableContext observable_ctx{};
    observable_ctx.ts_exchange = frame.ts_exchange;
    observable_ctx.step_seq = step_state.step_seq;
    observable_ctx.replay_exhausted = false;
    observable_ctx.market_payload = std::move(frame.market_payload);
    observable_ctx.position_snapshot = &runtime_state.positions;
    observable_ctx.order_snapshot = &runtime_state.orders;
    observable_ctx.account_state_version = step_state.account_state_version;
    update_snapshot_state(snapshot_state, step_state, observable_ctx);
    Output::ChannelPublisher::PublishStep(exchange_, observable_ctx);
    publish_position_order_channels(exchange_, runtime_state, step_state);
    step_state.channels_closed = false;
    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
