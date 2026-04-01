#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
namespace {
constexpr size_t kReplayPayloadInitialPoolSize = 3;

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

void drop_stale_funding_heap_entries(State::StepKernelState& state)
{
    while (!state.next_funding_ts_heap.empty()) {
        const auto top = state.next_funding_ts_heap.top();
        if (top.sym_id >= state.has_next_funding_ts.size() ||
            !state.has_next_funding_ts[top.sym_id] ||
            top.sym_id >= state.next_funding_ts_by_symbol.size() ||
            state.next_funding_ts_by_symbol[top.sym_id] != top.ts) {
            state.next_funding_ts_heap.pop();
            continue;
        }
        break;
    }
}

State::ReplayPayloadBuffer make_payload_buffer(size_t symbol_count)
{
    State::ReplayPayloadBuffer buffer{};
    buffer.dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    buffer.dto->trade_klines_by_id.resize(symbol_count);
    buffer.dto->mark_klines_by_id.resize(symbol_count);
    buffer.dto->index_klines_by_id.resize(symbol_count);
    buffer.dto->funding_by_id.resize(symbol_count);
    buffer.touched_trade_ids.reserve(symbol_count);
    buffer.touched_mark_ids.reserve(symbol_count);
    buffer.touched_index_ids.reserve(symbol_count);
    buffer.touched_funding_ids.reserve(symbol_count);
    return buffer;
}

void ensure_payload_buffer_shape(State::ReplayPayloadBuffer& buffer, size_t symbol_count)
{
    if (buffer.dto == nullptr) {
        buffer = make_payload_buffer(symbol_count);
        return;
    }
    if (buffer.dto->trade_klines_by_id.size() != symbol_count) {
        buffer.dto->trade_klines_by_id.resize(symbol_count);
        buffer.dto->mark_klines_by_id.resize(symbol_count);
        buffer.dto->index_klines_by_id.resize(symbol_count);
        buffer.dto->funding_by_id.resize(symbol_count);
        buffer.touched_trade_ids.clear();
        buffer.touched_mark_ids.clear();
        buffer.touched_index_ids.clear();
        buffer.touched_funding_ids.clear();
        buffer.touched_trade_ids.reserve(symbol_count);
        buffer.touched_mark_ids.reserve(symbol_count);
        buffer.touched_index_ids.reserve(symbol_count);
        buffer.touched_funding_ids.reserve(symbol_count);
        return;
    }
    for (const auto symbol_id : buffer.touched_trade_ids) {
        if (symbol_id < buffer.dto->trade_klines_by_id.size()) {
            buffer.dto->trade_klines_by_id[symbol_id].reset();
        }
    }
    for (const auto symbol_id : buffer.touched_mark_ids) {
        if (symbol_id < buffer.dto->mark_klines_by_id.size()) {
            buffer.dto->mark_klines_by_id[symbol_id].reset();
        }
    }
    for (const auto symbol_id : buffer.touched_index_ids) {
        if (symbol_id < buffer.dto->index_klines_by_id.size()) {
            buffer.dto->index_klines_by_id[symbol_id].reset();
        }
    }
    for (const auto symbol_id : buffer.touched_funding_ids) {
        if (symbol_id < buffer.dto->funding_by_id.size()) {
            buffer.dto->funding_by_id[symbol_id].reset();
        }
    }
    buffer.touched_trade_ids.clear();
    buffer.touched_mark_ids.clear();
    buffer.touched_index_ids.clear();
    buffer.touched_funding_ids.clear();
}

State::ReplayPayloadBuffer& acquire_payload_buffer(State::StepKernelState& state)
{
    const size_t symbol_count = state.symbols.size();
    if (state.replay_payload_pool.empty()) {
        state.replay_payload_pool.reserve(kReplayPayloadInitialPoolSize);
        for (size_t i = 0; i < kReplayPayloadInitialPoolSize; ++i) {
            state.replay_payload_pool.emplace_back(make_payload_buffer(symbol_count));
        }
        state.replay_payload_pool_cursor = 0;
    }

    size_t selected_idx = state.replay_payload_pool.size();
    for (size_t offset = 0; offset < state.replay_payload_pool.size(); ++offset) {
        const size_t idx = (state.replay_payload_pool_cursor + offset) % state.replay_payload_pool.size();
        auto& candidate = state.replay_payload_pool[idx];
        if (candidate.dto && candidate.dto.use_count() == 1) {
            selected_idx = idx;
            break;
        }
    }
    if (selected_idx == state.replay_payload_pool.size()) {
        state.replay_payload_pool.emplace_back(make_payload_buffer(symbol_count));
        selected_idx = state.replay_payload_pool.size() - 1;
    }

    state.replay_payload_pool_cursor = (selected_idx + 1) % state.replay_payload_pool.size();
    auto& selected = state.replay_payload_pool[selected_idx];
    ensure_payload_buffer_shape(selected, symbol_count);
    return selected;
}

} // namespace

MarketReplayStepFrame MarketReplayKernel::Next(State::StepKernelState& state)
{
    // Build one MultiKline DTO for the minimum timestamp across market + funding
    // timelines, then advance all symbols that match this timestamp.
    MarketReplayStepFrame out{};
    drop_stale_heap_entries(state);
    const uint64_t market_next_ts = state.next_ts_heap.empty()
        ? std::numeric_limits<uint64_t>::max()
        : state.next_ts_heap.top().ts;

    drop_stale_funding_heap_entries(state);
    const uint64_t funding_next_ts = state.next_funding_ts_heap.empty()
        ? std::numeric_limits<uint64_t>::max()
        : state.next_funding_ts_heap.top().ts;

    const uint64_t ts = std::min(market_next_ts, funding_next_ts);
    if (ts == std::numeric_limits<uint64_t>::max()) {
        return out;
    }

    out.has_next = true;
    out.ts_exchange = ts;

    auto& payload_buffer = acquire_payload_buffer(state);
    auto dto = payload_buffer.dto;
    dto->Timestamp = ts;
    dto->symbols = state.symbols_shared;
    const size_t symbol_count = state.symbols.size();
    if (state.replay_has_trade_kline_by_symbol.size() != symbol_count) {
        state.replay_has_trade_kline_by_symbol.assign(symbol_count, 0);
        state.replay_trade_open_by_symbol.assign(symbol_count, 0.0);
        state.replay_trade_high_by_symbol.assign(symbol_count, 0.0);
        state.replay_trade_low_by_symbol.assign(symbol_count, 0.0);
        state.replay_trade_close_by_symbol.assign(symbol_count, 0.0);
        state.replay_trade_volume_by_symbol.assign(symbol_count, 0.0);
        state.replay_trade_taker_buy_base_volume_by_symbol.assign(symbol_count, 0.0);
        state.replay_has_mark_price_by_symbol.assign(symbol_count, 0);
        state.replay_mark_price_by_symbol.assign(symbol_count, 0.0);
        state.replay_has_index_price_by_symbol.assign(symbol_count, 0);
        state.replay_index_price_by_symbol.assign(symbol_count, 0.0);
        state.replay_has_funding_by_symbol.assign(symbol_count, 0);
        state.replay_funding_rate_by_symbol.assign(symbol_count, 0.0);
        state.replay_funding_time_by_symbol.assign(symbol_count, 0);
    }
    else {
        std::fill(state.replay_has_trade_kline_by_symbol.begin(), state.replay_has_trade_kline_by_symbol.end(), 0);
        std::fill(state.replay_has_mark_price_by_symbol.begin(), state.replay_has_mark_price_by_symbol.end(), 0);
        std::fill(state.replay_has_index_price_by_symbol.begin(), state.replay_has_index_price_by_symbol.end(), 0);
        std::fill(state.replay_has_funding_by_symbol.begin(), state.replay_has_funding_by_symbol.end(), 0);
    }

    for (size_t i = 0; i < symbol_count; ++i) {
        if (i >= state.has_next_ts.size() || !state.has_next_ts[i]) {
            // no market kline for this symbol in this step
        }
        else if (state.next_ts_by_symbol[i] == ts) {
            const size_t cur = state.replay_cursor[i];
            const auto& trade_kline = state.market_data[i].get_kline(cur);
            dto->trade_klines_by_id[i] = trade_kline;
            payload_buffer.touched_trade_ids.push_back(i);
            state.replay_has_trade_kline_by_symbol[i] = 1;
            state.replay_trade_open_by_symbol[i] = trade_kline.OpenPrice;
            state.replay_trade_high_by_symbol[i] = trade_kline.HighPrice;
            state.replay_trade_low_by_symbol[i] = trade_kline.LowPrice;
            state.replay_trade_close_by_symbol[i] = trade_kline.ClosePrice;
            state.replay_trade_volume_by_symbol[i] = trade_kline.Volume;
            state.replay_trade_taker_buy_base_volume_by_symbol[i] = trade_kline.TakerBuyBaseVolume;

            const size_t next = cur + 1;
            state.replay_cursor[i] = next;
            if (next < state.market_data[i].get_klines_count()) {
                const uint64_t next_ts = state.market_data[i].get_kline(next).Timestamp;
                state.next_ts_by_symbol[i] = next_ts;
                state.next_ts_heap.push(State::StepKernelHeapItem{ next_ts, i });
            }
            else {
                state.has_next_ts[i] = 0;
            }
        }

        if (i < state.has_next_mark_ts.size() && state.has_next_mark_ts[i]) {
            const int32_t data_id = state.mark_data_id_by_symbol[i];
            if (data_id < 0 || static_cast<size_t>(data_id) >= state.mark_data_pool.size()) {
                state.has_next_mark_ts[i] = 0;
            }
            else {
                auto& mark_data = state.mark_data_pool[static_cast<size_t>(data_id)];
                size_t cursor = state.mark_cursor_by_symbol[i];
                const size_t total = mark_data.get_klines_count();
                while (cursor < total && mark_data.get_kline(cursor).Timestamp < ts) {
                    ++cursor;
                }
                if (cursor < total && mark_data.get_kline(cursor).Timestamp == ts) {
                    const auto& kline = mark_data.get_kline(cursor);
                    dto->mark_klines_by_id[i] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(
                        kline.Timestamp,
                        kline.ClosePrice);
                    payload_buffer.touched_mark_ids.push_back(i);
                    state.replay_has_mark_price_by_symbol[i] = 1;
                    state.replay_mark_price_by_symbol[i] = kline.ClosePrice;
                    ++cursor;
                }
                state.mark_cursor_by_symbol[i] = cursor;
                if (cursor < total) {
                    state.next_mark_ts_by_symbol[i] = mark_data.get_kline(cursor).Timestamp;
                }
                else {
                    state.has_next_mark_ts[i] = 0;
                }
            }
        }

        if (i < state.has_next_index_ts.size() && state.has_next_index_ts[i]) {
            const int32_t data_id = state.index_data_id_by_symbol[i];
            if (data_id < 0 || static_cast<size_t>(data_id) >= state.index_data_pool.size()) {
                state.has_next_index_ts[i] = 0;
            }
            else {
                auto& index_data = state.index_data_pool[static_cast<size_t>(data_id)];
                size_t cursor = state.index_cursor_by_symbol[i];
                const size_t total = index_data.get_klines_count();
                while (cursor < total && index_data.get_kline(cursor).Timestamp < ts) {
                    ++cursor;
                }
                if (cursor < total && index_data.get_kline(cursor).Timestamp == ts) {
                    const auto& kline = index_data.get_kline(cursor);
                    dto->index_klines_by_id[i] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(
                        kline.Timestamp,
                        kline.ClosePrice);
                    payload_buffer.touched_index_ids.push_back(i);
                    state.replay_has_index_price_by_symbol[i] = 1;
                    state.replay_index_price_by_symbol[i] = kline.ClosePrice;
                    ++cursor;
                }
                state.index_cursor_by_symbol[i] = cursor;
                if (cursor < total) {
                    state.next_index_ts_by_symbol[i] = index_data.get_kline(cursor).Timestamp;
                }
                else {
                    state.has_next_index_ts[i] = 0;
                }
            }
        }

        if (i >= state.has_next_funding_ts.size() || !state.has_next_funding_ts[i]) {
            continue;
        }
        if (state.next_funding_ts_by_symbol[i] != ts) {
            continue;
        }

        const int32_t data_id = state.funding_data_id_by_symbol[i];
        if (data_id < 0 || static_cast<size_t>(data_id) >= state.funding_data_pool.size()) {
            state.has_next_funding_ts[i] = 0;
            continue;
        }
        auto& funding_data = state.funding_data_pool[static_cast<size_t>(data_id)];
        size_t cursor = state.funding_cursor_by_symbol[i];
        if (cursor >= funding_data.get_count()) {
            state.has_next_funding_ts[i] = 0;
            continue;
        }

        dto->funding_by_id[i] = funding_data.get_funding(cursor);
        payload_buffer.touched_funding_ids.push_back(i);
        state.replay_has_funding_by_symbol[i] = 1;
        state.replay_funding_rate_by_symbol[i] = dto->funding_by_id[i]->Rate;
        state.replay_funding_time_by_symbol[i] = dto->funding_by_id[i]->FundingTime;
        ++cursor;
        state.funding_cursor_by_symbol[i] = cursor;
        if (cursor < funding_data.get_count()) {
            state.next_funding_ts_by_symbol[i] = funding_data.get_funding(cursor).FundingTime;
            state.next_funding_ts_heap.push(State::StepKernelHeapItem{ state.next_funding_ts_by_symbol[i], i });
        }
        else {
            state.has_next_funding_ts[i] = 0;
        }
    }

    out.market_payload = std::move(dto);
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
