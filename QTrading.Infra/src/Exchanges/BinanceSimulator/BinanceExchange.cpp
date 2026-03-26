#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

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
    // Phase-1/2/3 bootstrap:
    // - build replay state, channels, and initial status snapshot
    // - logger is intentionally unused in the current skeleton stage
    static_cast<void>(logger);
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
    step_kernel_state_->market_data.reserve(datasets.size());
    step_kernel_state_->replay_cursor.assign(datasets.size(), 0);
    step_kernel_state_->next_ts_by_symbol.assign(datasets.size(), 0);
    step_kernel_state_->has_next_ts.assign(datasets.size(), 0);

    for (size_t i = 0; i < datasets.size(); ++i) {
        const auto& ds = datasets[i];
        step_kernel_state_->symbols.push_back(ds.symbol);
        step_kernel_state_->symbol_to_id.emplace(ds.symbol, i);
        step_kernel_state_->market_data.emplace_back(ds.symbol, ds.kline_csv);
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
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
