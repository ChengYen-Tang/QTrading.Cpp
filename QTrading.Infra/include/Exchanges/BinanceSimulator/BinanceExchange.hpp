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

namespace QTrading::Infra::Exchanges::BinanceSim {

using MultiKlinePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace State {
struct BinanceExchangeRuntimeState;
}

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

    bool step() override;
    const std::vector<QTrading::dto::Position>& get_all_positions() const override;
    const std::vector<QTrading::dto::Order>& get_all_open_orders() const override;

    void set_symbol_leverage(const std::string& symbol, double new_leverage) override;
    double get_symbol_leverage(const std::string& symbol) const override;
    void apply_simulation_config(const SimulationConfig& config);
    const SimulationConfig& simulation_config() const;
    void close() override;

    void FillStatusSnapshot(StatusSnapshot& out) const;
    Account& account_state() noexcept;
    const Account& account_state() const noexcept;

    BinanceExchange(const BinanceExchange&) = delete;
    BinanceExchange& operator=(const BinanceExchange&) = delete;
    BinanceExchange(BinanceExchange&&) = delete;
    BinanceExchange& operator=(BinanceExchange&&) = delete;

private:
    void initialize_channels_();

    std::shared_ptr<Account> account_;
    std::unique_ptr<State::BinanceExchangeRuntimeState> runtime_state_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim
