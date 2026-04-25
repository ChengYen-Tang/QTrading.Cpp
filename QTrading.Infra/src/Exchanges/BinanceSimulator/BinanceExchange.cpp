#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <future>
#include <limits>
#include <utility>

#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"
#include "Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.hpp"
#include "Exchanges/BinanceSimulator/Output/SnapshotBuilder.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
namespace {
constexpr size_t kReplayPayloadPrewarmPoolSize = 3;

constexpr double kEpsilon = 1e-12;

void rebuild_visible_positions_cache(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    auto& cache = runtime_state.visible_positions_cache;
    cache = runtime_state.positions;
    cache.reserve(runtime_state.positions.size() + runtime_state.spot_inventory_qty_by_symbol.size());

    const size_t symbol_count = std::min(
        step_state.symbols.size(),
        runtime_state.spot_inventory_qty_by_symbol.size());
    for (size_t symbol_id = 0; symbol_id < symbol_count; ++symbol_id) {
        const double qty = runtime_state.spot_inventory_qty_by_symbol[symbol_id];
        if (!(qty > kEpsilon)) {
            continue;
        }

        QTrading::dto::Position synthetic{};
        synthetic.id =
            symbol_id < runtime_state.spot_inventory_position_id_by_symbol.size()
                ? runtime_state.spot_inventory_position_id_by_symbol[symbol_id]
                : 0;
        synthetic.order_id = 0;
        synthetic.symbol = step_state.symbols[symbol_id];
        synthetic.quantity = qty;
        synthetic.entry_price =
            symbol_id < runtime_state.spot_inventory_entry_price_by_symbol.size()
                ? runtime_state.spot_inventory_entry_price_by_symbol[symbol_id]
                : 0.0;
        synthetic.is_long = true;
        synthetic.notional = synthetic.quantity * synthetic.entry_price;
        synthetic.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
        cache.emplace_back(std::move(synthetic));
    }

    runtime_state.visible_positions_cache_version = runtime_state.positions_version;
}

const std::vector<QTrading::dto::Position>& ensure_visible_positions_cache(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    if (runtime_state.visible_positions_cache_version != runtime_state.positions_version) {
        rebuild_visible_positions_cache(runtime_state, step_state);
    }
    return runtime_state.visible_positions_cache;
}
}

BinanceExchange::BinanceExchange(const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger, const Account::AccountInitConfig& account_init, uint64_t run_id)
    : spot(*this),
      perp(*this),
      account(*this),
      account_(std::make_shared<Account>(account_init)),
      runtime_state_(std::make_unique<State::BinanceExchangeRuntimeState>()),
      step_kernel_state_(std::make_unique<State::StepKernelState>()),
      snapshot_state_(std::make_unique<State::SnapshotState>())
{
    // Build replay state, channels, and the initial status snapshot.
    runtime_state_->logger = std::move(logger);
    runtime_state_->hedge_mode = account_init.hedge_mode;
    runtime_state_->strict_binance_mode = account_init.strict_binance_mode;
    runtime_state_->merge_positions_enabled = account_init.merge_positions_enabled;
    runtime_state_->vip_level = account_init.vip_level;
    initialize_step_kernel_state_(datasets, run_id);
    initialize_channels_();
    runtime_state_->last_status_snapshot =
        Bootstrap::BuildInitialStatusSnapshot(account_init, runtime_state_->simulation_config);
}

BinanceExchange::~BinanceExchange() = default;

bool BinanceExchange::step()
{
    // Facade-only forwarding keeps public API stable while kernel evolves.
    return Application::StepKernel(*this).run_step();
}

const std::vector<QTrading::dto::Position>& BinanceExchange::get_all_positions() const
{
    return ensure_visible_positions_cache(*runtime_state_, *step_kernel_state_);
}

const std::vector<QTrading::dto::Order>& BinanceExchange::get_all_open_orders() const
{
    return runtime_state_->orders;
}

void BinanceExchange::close()
{
    IExchange<MultiKlinePtr>::close();
}

void BinanceExchange::FillStatusSnapshot(StatusSnapshot& out) const
{
    // Snapshot read path is centralized in Output::SnapshotBuilder.
    Output::SnapshotBuilder::Fill(*this, out);
}

void BinanceExchange::apply_simulation_config(const SimulationConfig& config)
{
    runtime_state_->simulation_config = config;
    runtime_state_->last_status_snapshot.uncertainty_band_bps = config.uncertainty_band_bps;
}

const BinanceExchange::SimulationConfig& BinanceExchange::simulation_config() const
{
    return runtime_state_->simulation_config;
}

Account& BinanceExchange::account_state() noexcept
{
    return *account_;
}

const Account& BinanceExchange::account_state() const noexcept
{
    return *account_;
}

void BinanceExchange::initialize_channels_()
{
    // Contract: market channel bounded; position/order channels unbounded.
    market_channel = QTrading::Utils::Queue::ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(
        8, QTrading::Utils::Queue::OverflowPolicy::DropOldest);
    position_channel = QTrading::Utils::Queue::ChannelFactory::CreateUnboundedChannel<std::vector<QTrading::dto::Position>>();
    order_channel = QTrading::Utils::Queue::ChannelFactory::CreateUnboundedChannel<std::vector<QTrading::dto::Order>>();
}

void BinanceExchange::initialize_step_kernel_state_(const std::vector<SymbolDataset>& datasets, uint64_t run_id)
{
    // One-time replay state construction; no per-step allocations here.
    const size_t symbol_count = datasets.size();
    step_kernel_state_->run_id = run_id;
    step_kernel_state_->symbols.resize(symbol_count);
    step_kernel_state_->symbol_to_id.reserve(symbol_count);
    step_kernel_state_->symbol_instrument_type_by_id.resize(symbol_count);
    step_kernel_state_->symbol_spec_by_id.resize(symbol_count);
    step_kernel_state_->symbol_maintenance_margin_tiers_by_id.resize(symbol_count);
    step_kernel_state_->replay_cursor.assign(symbol_count, 0);
    step_kernel_state_->funding_data_id_by_symbol.assign(symbol_count, -1);
    step_kernel_state_->mark_data_id_by_symbol.assign(symbol_count, -1);
    step_kernel_state_->index_data_id_by_symbol.assign(symbol_count, -1);
    step_kernel_state_->funding_cursor_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->mark_cursor_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->index_cursor_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->next_funding_ts_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->next_mark_ts_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->next_index_ts_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->has_next_funding_ts.assign(symbol_count, 0);
    step_kernel_state_->has_next_mark_ts.assign(symbol_count, 0);
    step_kernel_state_->has_next_index_ts.assign(symbol_count, 0);
    step_kernel_state_->last_applied_funding_time_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->last_observed_funding_by_symbol.assign(symbol_count, std::nullopt);
    step_kernel_state_->next_ts_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->has_next_ts.assign(symbol_count, 0);
    step_kernel_state_->replay_has_trade_kline_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->replay_trade_open_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_trade_high_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_trade_low_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_trade_close_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_trade_volume_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_trade_taker_buy_base_volume_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_has_mark_price_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->replay_mark_price_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_has_index_price_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->replay_index_price_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_has_funding_by_symbol.assign(symbol_count, 0);
    step_kernel_state_->replay_funding_rate_by_symbol.assign(symbol_count, 0.0);
    step_kernel_state_->replay_funding_time_by_symbol.assign(symbol_count, 0);

    size_t funding_count = 0;
    size_t mark_count = 0;
    size_t index_count = 0;
    for (size_t i = 0; i < symbol_count; ++i) {
        const auto& ds = datasets[i];
        step_kernel_state_->symbols[i] = ds.symbol;
        step_kernel_state_->symbol_to_id.emplace(ds.symbol, i);
        const auto instrument_type = ds.instrument_type.value_or(QTrading::Dto::Trading::InstrumentType::Perp);
        step_kernel_state_->symbol_instrument_type_by_id[i] = instrument_type;
        step_kernel_state_->symbol_spec_by_id[i] =
            instrument_type == QTrading::Dto::Trading::InstrumentType::Spot
                ? QTrading::Dto::Trading::SpotInstrumentSpec()
                : QTrading::Dto::Trading::PerpInstrumentSpec();
        if (ds.funding_csv.has_value()) {
            step_kernel_state_->funding_data_id_by_symbol[i] = static_cast<int32_t>(funding_count++);
        }
        if (ds.mark_kline_csv.has_value() && !ds.mark_kline_csv->empty()) {
            step_kernel_state_->mark_data_id_by_symbol[i] = static_cast<int32_t>(mark_count++);
        }
        if (ds.index_kline_csv.has_value() && !ds.index_kline_csv->empty()) {
            step_kernel_state_->index_data_id_by_symbol[i] = static_cast<int32_t>(index_count++);
        }
    }

    std::vector<std::optional<MarketData>> market_slots(symbol_count);
    std::vector<std::optional<FundingRateData>> funding_slots(funding_count);
    std::vector<std::optional<MarketData>> mark_slots(mark_count);
    std::vector<std::optional<MarketData>> index_slots(index_count);
    std::vector<std::future<void>> load_tasks{};
    load_tasks.reserve(symbol_count);

    for (size_t i = 0; i < symbol_count; ++i) {
        load_tasks.push_back(std::async(std::launch::async, [&, i]() {
            const auto& ds = datasets[i];
            market_slots[i].emplace(ds.symbol, ds.kline_csv);

            const int32_t funding_id = step_kernel_state_->funding_data_id_by_symbol[i];
            if (funding_id >= 0 && ds.funding_csv.has_value()) {
                funding_slots[static_cast<size_t>(funding_id)].emplace(ds.symbol, *ds.funding_csv);
            }

            const int32_t mark_id = step_kernel_state_->mark_data_id_by_symbol[i];
            if (mark_id >= 0 && ds.mark_kline_csv.has_value() && !ds.mark_kline_csv->empty()) {
                mark_slots[static_cast<size_t>(mark_id)].emplace(ds.symbol, *ds.mark_kline_csv);
            }

            const int32_t index_id = step_kernel_state_->index_data_id_by_symbol[i];
            if (index_id >= 0 && ds.index_kline_csv.has_value() && !ds.index_kline_csv->empty()) {
                index_slots[static_cast<size_t>(index_id)].emplace(ds.symbol, *ds.index_kline_csv);
            }
        }));
    }
    for (auto& task : load_tasks) {
        task.get();
    }

    step_kernel_state_->market_data.clear();
    step_kernel_state_->market_data.reserve(symbol_count);
    for (size_t i = 0; i < symbol_count; ++i) {
        step_kernel_state_->market_data.push_back(std::move(*market_slots[i]));
    }

    step_kernel_state_->funding_data_pool.clear();
    step_kernel_state_->funding_data_pool.reserve(funding_count);
    for (size_t i = 0; i < funding_count; ++i) {
        step_kernel_state_->funding_data_pool.push_back(std::move(*funding_slots[i]));
    }

    step_kernel_state_->mark_data_pool.clear();
    step_kernel_state_->mark_data_pool.reserve(mark_count);
    for (size_t i = 0; i < mark_count; ++i) {
        step_kernel_state_->mark_data_pool.push_back(std::move(*mark_slots[i]));
    }

    step_kernel_state_->index_data_pool.clear();
    step_kernel_state_->index_data_pool.reserve(index_count);
    for (size_t i = 0; i < index_count; ++i) {
        step_kernel_state_->index_data_pool.push_back(std::move(*index_slots[i]));
    }

    for (size_t i = 0; i < symbol_count; ++i) {
        const int32_t funding_id = step_kernel_state_->funding_data_id_by_symbol[i];
        if (funding_id >= 0) {
            const auto& funding_data = step_kernel_state_->funding_data_pool[static_cast<size_t>(funding_id)];
            if (funding_data.get_count() > 0) {
                step_kernel_state_->next_funding_ts_by_symbol[i] = funding_data.get_funding(0).FundingTime;
                step_kernel_state_->has_next_funding_ts[i] = 1;
                step_kernel_state_->next_funding_ts_heap.push(State::StepKernelHeapItem{
                    step_kernel_state_->next_funding_ts_by_symbol[i],
                    i
                });
            }
        }
        const int32_t mark_id = step_kernel_state_->mark_data_id_by_symbol[i];
        if (mark_id >= 0) {
            const auto& mark_data = step_kernel_state_->mark_data_pool[static_cast<size_t>(mark_id)];
            if (mark_data.get_klines_count() > 0) {
                step_kernel_state_->next_mark_ts_by_symbol[i] = mark_data.get_kline(0).Timestamp;
                step_kernel_state_->has_next_mark_ts[i] = 1;
            }
        }
        const int32_t index_id = step_kernel_state_->index_data_id_by_symbol[i];
        if (index_id >= 0) {
            const auto& index_data = step_kernel_state_->index_data_pool[static_cast<size_t>(index_id)];
            if (index_data.get_klines_count() > 0) {
                step_kernel_state_->next_index_ts_by_symbol[i] = index_data.get_kline(0).Timestamp;
                step_kernel_state_->has_next_index_ts[i] = 1;
            }
        }
        if (step_kernel_state_->market_data[i].get_klines_count() > 0) {
            const uint64_t ts = step_kernel_state_->market_data[i].get_kline(0).Timestamp;
            step_kernel_state_->next_ts_by_symbol[i] = ts;
            step_kernel_state_->has_next_ts[i] = 1;
            step_kernel_state_->next_ts_heap.push(State::StepKernelHeapItem{ ts, i });
        }
    }

    step_kernel_state_->symbols_shared =
        std::make_shared<const std::vector<std::string>>(step_kernel_state_->symbols);
    snapshot_state_->symbols_shared = step_kernel_state_->symbols_shared;
    snapshot_state_->last_trade_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0.0);
    snapshot_state_->has_last_trade_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0);
    snapshot_state_->last_mark_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0.0);
    snapshot_state_->has_last_mark_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0);
    snapshot_state_->last_mark_price_ts_by_symbol.assign(
        step_kernel_state_->symbols.size(),
        std::numeric_limits<uint64_t>::max());
    snapshot_state_->last_mark_price_source_by_symbol.assign(
        step_kernel_state_->symbols.size(),
        static_cast<int32_t>(Contracts::ReferencePriceSource::None));
    snapshot_state_->last_index_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0.0);
    snapshot_state_->has_last_index_price_by_symbol.assign(step_kernel_state_->symbols.size(), 0);
    snapshot_state_->last_index_price_ts_by_symbol.assign(
        step_kernel_state_->symbols.size(),
        std::numeric_limits<uint64_t>::max());
    snapshot_state_->last_index_price_source_by_symbol.assign(
        step_kernel_state_->symbols.size(),
        static_cast<int32_t>(Contracts::ReferencePriceSource::None));
    snapshot_state_->price_rows_by_symbol.assign(
        step_kernel_state_->symbols.size(),
        State::SnapshotPriceRowById{});
    snapshot_state_->price_row_dirty_by_symbol.assign(step_kernel_state_->symbols.size(), 1);
    snapshot_state_->dirty_price_symbol_ids.clear();
    snapshot_state_->dirty_price_symbol_ids.reserve(step_kernel_state_->symbols.size());
    for (size_t i = 0; i < step_kernel_state_->symbols.size(); ++i) {
        snapshot_state_->dirty_price_symbol_ids.push_back(i);
    }
    snapshot_state_->price_rows_version = step_kernel_state_->symbols.empty() ? 0 : 1;

    step_kernel_state_->replay_payload_pool.clear();
    step_kernel_state_->replay_payload_pool.reserve(kReplayPayloadPrewarmPoolSize);
    for (size_t i = 0; i < kReplayPayloadPrewarmPoolSize; ++i) {
        State::ReplayPayloadBuffer buffer{};
        buffer.dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
        buffer.dto->symbols = step_kernel_state_->symbols_shared;
        buffer.dto->trade_klines_by_id.resize(step_kernel_state_->symbols.size());
        buffer.dto->mark_klines_by_id.resize(step_kernel_state_->symbols.size());
        buffer.dto->index_klines_by_id.resize(step_kernel_state_->symbols.size());
        buffer.dto->funding_by_id.resize(step_kernel_state_->symbols.size());
        buffer.touched_trade_ids.reserve(step_kernel_state_->symbols.size());
        buffer.touched_mark_ids.reserve(step_kernel_state_->symbols.size());
        buffer.touched_index_ids.reserve(step_kernel_state_->symbols.size());
        buffer.touched_funding_ids.reserve(step_kernel_state_->symbols.size());
        step_kernel_state_->replay_payload_pool.emplace_back(std::move(buffer));
    }
    step_kernel_state_->replay_payload_pool_cursor = 0;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
