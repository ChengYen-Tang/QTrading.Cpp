#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <utility>

#include "Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

BinanceExchange::BinanceExchange(const std::vector<SymbolDataset>& datasets,
    std::shared_ptr<QTrading::Log::Logger> logger, const Account::AccountInitConfig& account_init, uint64_t run_id)
    : spot(*this),
      perp(*this),
      account(*this),
      account_(std::make_shared<Account>(account_init)),
      runtime_state_(std::make_unique<State::BinanceExchangeRuntimeState>())
{
    static_cast<void>(logger);
    static_cast<void>(datasets);
    runtime_state_->run_id = run_id;
    initialize_channels_();
    runtime_state_->last_status_snapshot =
        Bootstrap::BuildInitialStatusSnapshot(account_init, runtime_state_->simulation_config);
}

BinanceExchange::~BinanceExchange() = default;

bool BinanceExchange::step()
{
    Support::ThrowNotImplemented("BinanceExchange::step");
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
    out = runtime_state_->last_status_snapshot;
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
    market_channel = QTrading::Utils::Queue::ChannelFactory::CreateUnboundedChannel<MultiKlinePtr>();
    position_channel = QTrading::Utils::Queue::ChannelFactory::CreateUnboundedChannel<std::vector<QTrading::dto::Position>>();
    order_channel = QTrading::Utils::Queue::ChannelFactory::CreateUnboundedChannel<std::vector<QTrading::dto::Order>>();
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
