#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"

#include <utility>

#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
namespace {

// Removes heap entries invalidated by cursor advances.
// This keeps timestamp extraction deterministic without rebuilding the heap.
void drop_stale_heap_entries(State::StepKernelState& state)
{
    while (!state.next_ts_heap.empty()) {
        const auto top = state.next_ts_heap.top();
        if (top.sym_id >= state.has_next_ts.size() ||
            !state.has_next_ts[top.sym_id] ||
            state.next_ts_by_symbol[top.sym_id] != top.ts) {
            state.next_ts_heap.pop();
            continue;
        }
        break;
    }
}

} // namespace

MarketReplayKernel::StepFrame MarketReplayKernel::Next(State::StepKernelState& state)
{
    // Build one MultiKline DTO for the minimum timestamp and advance all
    // symbols that match this timestamp.
    StepFrame out{};
    drop_stale_heap_entries(state);
    if (state.next_ts_heap.empty()) {
        return out;
    }

    const uint64_t ts = state.next_ts_heap.top().ts;
    out.has_next = true;
    out.ts_exchange = ts;

    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    dto->symbols = state.symbols_shared;
    const size_t symbol_count = state.symbols.size();
    dto->trade_klines_by_id.resize(symbol_count);
    dto->mark_klines_by_id.resize(symbol_count);
    dto->index_klines_by_id.resize(symbol_count);
    dto->funding_by_id.resize(symbol_count);

    for (size_t i = 0; i < symbol_count; ++i) {
        if (i >= state.has_next_ts.size() || !state.has_next_ts[i]) {
            continue;
        }
        if (state.next_ts_by_symbol[i] != ts) {
            continue;
        }

        const size_t cur = state.replay_cursor[i];
        dto->trade_klines_by_id[i] = state.market_data[i].get_kline(cur);

        const size_t next = cur + 1;
        state.replay_cursor[i] = next;
        if (next < state.market_data[i].get_klines_count()) {
            const uint64_t next_ts = state.market_data[i].get_kline(next).Timestamp;
            state.next_ts_by_symbol[i] = next_ts;
            state.next_ts_heap.push(State::StepKernelState::HeapItem{ next_ts, i });
        }
        else {
            state.has_next_ts[i] = 0;
        }
    }

    out.market_payload = std::move(dto);
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
