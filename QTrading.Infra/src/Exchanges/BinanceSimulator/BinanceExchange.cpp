#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <limits>
#include <utility>

#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"
#include "Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.hpp"
#include "Exchanges/BinanceSimulator/Output/SnapshotBuilder.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

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
    return runtime_state_->positions;
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
    step_kernel_state_->run_id = run_id;
    step_kernel_state_->symbols.reserve(datasets.size());
    step_kernel_state_->symbol_to_id.reserve(datasets.size());
    step_kernel_state_->symbol_instrument_type_by_id.reserve(datasets.size());
    step_kernel_state_->symbol_spec_by_id.reserve(datasets.size());
    step_kernel_state_->market_data.reserve(datasets.size());
    step_kernel_state_->funding_data_pool.reserve(datasets.size());
    step_kernel_state_->mark_data_pool.reserve(datasets.size());
    step_kernel_state_->index_data_pool.reserve(datasets.size());
    step_kernel_state_->replay_cursor.assign(datasets.size(), 0);
    step_kernel_state_->funding_data_id_by_symbol.assign(datasets.size(), -1);
    step_kernel_state_->mark_data_id_by_symbol.assign(datasets.size(), -1);
    step_kernel_state_->index_data_id_by_symbol.assign(datasets.size(), -1);
    step_kernel_state_->funding_cursor_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->mark_cursor_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->index_cursor_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->next_funding_ts_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->next_mark_ts_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->next_index_ts_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->has_next_funding_ts.assign(datasets.size(), 0);
    step_kernel_state_->has_next_mark_ts.assign(datasets.size(), 0);
    step_kernel_state_->has_next_index_ts.assign(datasets.size(), 0);
    step_kernel_state_->last_applied_funding_time_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->next_ts_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->has_next_ts.assign(datasets.size(), 0);

    for (size_t i = 0; i < datasets.size(); ++i) {
        const auto& ds = datasets[i];
        step_kernel_state_->symbols.push_back(ds.symbol);
        step_kernel_state_->symbol_to_id.emplace(ds.symbol, i);
        const auto instrument_type = ds.instrument_type.value_or(QTrading::Dto::Trading::InstrumentType::Perp);
        step_kernel_state_->symbol_instrument_type_by_id.push_back(instrument_type);
        step_kernel_state_->symbol_spec_by_id.push_back(
            instrument_type == QTrading::Dto::Trading::InstrumentType::Spot
                ? QTrading::Dto::Trading::SpotInstrumentSpec()
                : QTrading::Dto::Trading::PerpInstrumentSpec());
        step_kernel_state_->market_data.emplace_back(ds.symbol, ds.kline_csv);
        if (ds.funding_csv.has_value()) {
            step_kernel_state_->funding_data_id_by_symbol[i] =
                static_cast<int32_t>(step_kernel_state_->funding_data_pool.size());
            step_kernel_state_->funding_data_pool.emplace_back(ds.symbol, *ds.funding_csv);
            const auto& funding_data = step_kernel_state_->funding_data_pool.back();
            if (funding_data.get_count() > 0) {
                step_kernel_state_->next_funding_ts_by_symbol[i] = funding_data.get_funding(0).FundingTime;
                step_kernel_state_->has_next_funding_ts[i] = 1;
                step_kernel_state_->next_funding_ts_heap.push(State::StepKernelHeapItem{
                    step_kernel_state_->next_funding_ts_by_symbol[i],
                    i
                });
            }
        }
        if (ds.mark_kline_csv.has_value() && !ds.mark_kline_csv->empty()) {
            step_kernel_state_->mark_data_id_by_symbol[i] =
                static_cast<int32_t>(step_kernel_state_->mark_data_pool.size());
            step_kernel_state_->mark_data_pool.emplace_back(ds.symbol, *ds.mark_kline_csv);
            const auto& mark_data = step_kernel_state_->mark_data_pool.back();
            if (mark_data.get_klines_count() > 0) {
                step_kernel_state_->next_mark_ts_by_symbol[i] = mark_data.get_kline(0).Timestamp;
                step_kernel_state_->has_next_mark_ts[i] = 1;
            }
        }
        if (ds.index_kline_csv.has_value() && !ds.index_kline_csv->empty()) {
            step_kernel_state_->index_data_id_by_symbol[i] =
                static_cast<int32_t>(step_kernel_state_->index_data_pool.size());
            step_kernel_state_->index_data_pool.emplace_back(ds.symbol, *ds.index_kline_csv);
            const auto& index_data = step_kernel_state_->index_data_pool.back();
            if (index_data.get_klines_count() > 0) {
                step_kernel_state_->next_index_ts_by_symbol[i] = index_data.get_kline(0).Timestamp;
                step_kernel_state_->has_next_index_ts[i] = 1;
            }
        }
        if (step_kernel_state_->market_data.back().get_klines_count() > 0) {
            const uint64_t ts = step_kernel_state_->market_data.back().get_kline(0).Timestamp;
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
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
