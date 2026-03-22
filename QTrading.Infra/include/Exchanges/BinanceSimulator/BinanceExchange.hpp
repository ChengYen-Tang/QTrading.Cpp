#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Dto/Account/BalanceSnapshot.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Exchanges/BinanceSimulator/Api/AccountApi.hpp"
#include "Exchanges/BinanceSimulator/Api/PerpApi.hpp"
#include "Exchanges/BinanceSimulator/Api/SpotApi.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Config/BinanceSimulationConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeStatusSnapshot.hpp"
#include "Exchanges/IExchange.h"
#include "Logger.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
struct SnapshotState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
class StepKernel;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Output {
class SnapshotBuilder;
}

namespace QTrading::Infra::Exchanges::BinanceSim {

using MultiKlinePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

/// Public simulator facade kept API-compatible while internals are rebuilt.
/// Runtime behavior is delegated to application/output/state modules.
class BinanceExchange final : public QTrading::Infra::Exchanges::IExchange<MultiKlinePtr> {
public:
    using SymbolDataset = Contracts::SymbolDataset;
    using StatusSnapshot = Contracts::StatusSnapshot;
    using SimulationConfig = Config::SimulationConfig;

    BinanceExchange(const std::vector<SymbolDataset>& datasets,
        std::shared_ptr<QTrading::Log::Logger> logger, const Account::AccountInitConfig& account_init,
        uint64_t run_id = 0);
    ~BinanceExchange();

    Api::SpotApi spot;
    Api::PerpApi perp;
    Api::AccountApi account;

    /// Main facade entrypoint. Delegates to StepKernel hot path.
    bool step() override;
    /// Read-only open position view (currently runtime-state backed skeleton).
    const std::vector<QTrading::dto::Position>& get_all_positions() const override;
    /// Read-only open order view (currently runtime-state backed skeleton).
    const std::vector<QTrading::dto::Order>& get_all_open_orders() const override;

    void set_symbol_leverage(const std::string& symbol, double new_leverage) override;
    double get_symbol_leverage(const std::string& symbol) const override;
    /// Applies simulation config knobs used by rebuilt kernels/builders.
    void apply_simulation_config(const SimulationConfig& config);
    const SimulationConfig& simulation_config() const;
    /// Closes public channels; part of outward contract surface.
    void close() override;

    /// Public snapshot read API. Internally uses Output::SnapshotBuilder path.
    void FillStatusSnapshot(StatusSnapshot& out) const;
    /// Exposes account state to API adapters/kernels without leaking ownership.
    Account& account_state() noexcept;
    const Account& account_state() const noexcept;

    BinanceExchange(const BinanceExchange&) = delete;
    BinanceExchange& operator=(const BinanceExchange&) = delete;
    BinanceExchange(BinanceExchange&&) = delete;
    BinanceExchange& operator=(BinanceExchange&&) = delete;

private:
    friend class Application::StepKernel;
    friend class Output::SnapshotBuilder;

    /// Initializes bounded/unbounded public channels according to contract.
    void initialize_channels_();
    /// Loads replay datasets into step kernel state and syncs snapshot symbol state.
    void initialize_step_kernel_state_(const std::vector<SymbolDataset>& datasets, uint64_t run_id);

    std::shared_ptr<Account> account_;
    std::unique_ptr<State::BinanceExchangeRuntimeState> runtime_state_;
    std::unique_ptr<State::StepKernelState> step_kernel_state_;
    std::unique_ptr<State::SnapshotState> snapshot_state_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim
