#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "LoggerBootstrap.hpp"
#include "ServiceHelpers.hpp"
#include "Builder/StrategyModuleBuilder.hpp"
#include "Diagnostics/Trace.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;
using BinanceExchange = QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
using SimAccount = QTrading::Infra::Exchanges::BinanceSim::Account;

/// @file QTrading.Service.cpp
/// @brief Defines the application entry point and wires together exchange and arbitrage pipeline modules.

/// @brief Main entry point for QTrading simulation.
/// @details 
///   1. Configure CSV input for each symbol  
///   2. Set up FeatherV2 logger and register modules  
///   3. Instantiate exchange simulator and arbitrage pipeline  
///   4. Run simulation loop until market data is exhausted  
///   5. Perform clean shutdown  
/// @return Returns 0 on successful completion.
int main()
{
    QTrading::Service::Helpers::InstallSignalHandlers();

#ifdef QTRADING_TRACE
    std::cerr << "[Service] QTRADING_TRACE enabled" << std::endl;
#else
    std::cerr << "[Service] QTRADING_TRACE disabled (define QTRADING_TRACE to enable trace logs)" << std::endl;
#endif
    std::cerr.flush();
    QTR_TRACE("service", "main begin");

    try {
        // Optional local debug range for IDE runs.
        // If environment variables are already set, they take priority.
        constexpr std::string_view kLocalSimStartDate = "";
        constexpr std::string_view kLocalSimEndDate = "";
        if (const char* env_start_date = std::getenv("QTR_SIM_START_DATE");
            (env_start_date == nullptr || env_start_date[0] == '\0') &&
            !kLocalSimStartDate.empty()) {
            QTrading::Service::Helpers::SetEnvVar("QTR_SIM_START_DATE", std::string(kLocalSimStartDate));
        }
        if (const char* env_end_date = std::getenv("QTR_SIM_END_DATE");
            (env_end_date == nullptr || env_end_date[0] == '\0') &&
            !kLocalSimEndDate.empty()) {
            QTrading::Service::Helpers::SetEnvVar("QTR_SIM_END_DATE", std::string(kLocalSimEndDate));
        }

        const std::string sim_start_date = (std::getenv("QTR_SIM_START_DATE") != nullptr)
            ? std::string(std::getenv("QTR_SIM_START_DATE"))
            : std::string();
        const std::string sim_end_date = (std::getenv("QTR_SIM_END_DATE") != nullptr)
            ? std::string(std::getenv("QTR_SIM_END_DATE"))
            : std::string();

        constexpr double kInitialSpotCash = 50'000'000.0;
        constexpr double kInitialPerpWallet = 50'000'000.0;
        constexpr int kVipLevel = 0;

        SimAccount::AccountInitConfig account_init{};
        account_init.spot_initial_cash = kInitialSpotCash;
        account_init.perp_initial_wallet = kInitialPerpWallet;
        account_init.vip_level = kVipLevel;

        // Change this one line to switch assembled strategy modules.
        constexpr auto kStrategyProfile = QTrading::Service::Builder::StrategyProfile::BasisArbitrage;
        const auto strategy_meta = QTrading::Service::Builder::GetStrategyMetadata(kStrategyProfile);
        const std::string strategy_name = strategy_meta.strategy_name;
        const std::filesystem::path strategy_config_path =
            QTrading::Service::Helpers::ResolveRepoRelativePath(
                std::filesystem::path(__FILE__),
                strategy_meta.config_relative_path);

        std::ostringstream strategy_params_builder;
        strategy_params_builder << "strategy_profile=" << strategy_meta.strategy_profile_param
                                << ";initial_spot_cash=" << kInitialSpotCash
                                << ";initial_perp_wallet=" << kInitialPerpWallet;
        strategy_params_builder << ";strategy_config=" << strategy_config_path.string();
        if (!sim_start_date.empty()) {
            strategy_params_builder << ";sim_start_date=" << sim_start_date;
        }
        if (!sim_end_date.empty()) {
            strategy_params_builder << ";sim_end_date=" << sim_end_date;
        }
        const std::string strategy_params = strategy_params_builder.str();

        /// @brief Mapping from symbol string to CSV file path.
        std::vector<BinanceExchange::SymbolDataset> symbolCsv = {
            {"BTCUSDT_SPOT",
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\Kline\Spot\BTCUSDT.csv)"),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                QTrading::Dto::Trading::InstrumentType::Spot},
            {"BTCUSDT_PERP",
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\Kline\UsdFutures\BTCUSDT.csv)"),
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\FundingRate\UsdFutures\BTCUSDT.csv)"),
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\MarkPriceKline\UsdFutures\BTCUSDT.csv)"),
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\IndexPriceKline\UsdFutures\BTCUSDT.csv)"),
                QTrading::Dto::Trading::InstrumentType::Perp}
        };
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types;
        instrument_types.reserve(symbolCsv.size());
        for (const auto& ds : symbolCsv) {
            if (ds.instrument_type.has_value()) {
                instrument_types.emplace(ds.symbol, *ds.instrument_type);
            }
        }

        std::filesystem::path logs_root = "logs";
        const auto logger_cfg = QTrading::Service::Helpers::BuildLoggerBootstrapConfig(
            logs_root,
            symbolCsv,
            strategy_name,
            "0.1",
            strategy_params);
        const auto logger_init = QTrading::Log::InitializeFeatherLogger(logger_cfg);
        const uint64_t run_id = logger_init.run_id;
        std::shared_ptr<QTrading::Log::SinkLogger> logger = logger_init.logger;

        std::cerr << "[Service] constructing exchange..." << std::endl;
        std::cerr.flush();
        // @brief Exchange simulator providing 1-minute MultiKlineDto.
        auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(
            symbolCsv, logger, account_init, run_id);
        std::cerr << "[Service] exchange constructed" << std::endl;
        std::cerr.flush();

        QTrading::Service::Builder::StrategyModuleConfigs module_configs;
        QTrading::Service::Builder::LoadStrategyModuleConfigs(
            kStrategyProfile,
            strategy_config_path,
            module_configs);

        // @brief Assemble selected strategy modules in one composition step.
        auto modules = QTrading::Service::Builder::BuildStrategyModules(
            kStrategyProfile,
            exchange,
            std::move(module_configs),
            instrument_types);

        std::cerr << "[Service] entering main loop..." << std::endl;
        std::cerr.flush();

        uint64_t steps = 0;
        auto last_progress = std::chrono::steady_clock::now();
        bool stop_logged = false;
        bool shutdown_initiated = false;
        auto Shutdown = [&](const char* reason) {
            if (shutdown_initiated) {
                return;
            }
            shutdown_initiated = true;
            if (reason && *reason) {
                std::cerr << "[Service] " << reason << std::endl;
            }
            exchange->close();
        };

        // @brief Main simulation loop: advance exchange until no more data.
        while (exchange->step()) {
            if (QTrading::Service::Helpers::StopRequested()) {
                if (!stop_logged) {
                    stop_logged = true;
                }
                Shutdown("stop requested, shutting down modules...");
                break;
            }
            QTR_TRACE("service", "exchange->step end ok");
            if (QTrading::Service::Helpers::StopRequested()) {
                if (!stop_logged) {
                    stop_logged = true;
                }
                Shutdown("stop requested, shutting down modules...");
                break;
            }
            modules.strategy->wait_for_done();

            ++steps;

            // Lightweight progress heartbeat every ~2s to help spot freezes.
            auto now = std::chrono::steady_clock::now();
            if (now - last_progress >= std::chrono::seconds(2)) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
                QTrading::Service::Helpers::EmitExchangeStatusLine(exchange);
#else
                std::cout << "[Service] steps=" << steps
                          << std::endl;
#endif
                last_progress = now;
            }
        }
        if (!QTrading::Service::Helpers::StopRequested()) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
            QTrading::Service::Helpers::EmitExchangeStatusLine(exchange);
#else
            std::cerr << "[Service] exchange->step returned false; exiting loop." << std::endl;
#endif
        }

        // @brief Clean shutdown: close channels, stop logger.
        Shutdown("shutting down modules...");
            logger->Stop();

        QTR_TRACE("service", "main end");
        std::cout << "Simulation completed." << std::endl;
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "[Service][FATAL] exception: " << ex.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "[Service][FATAL] unknown exception" << std::endl;
        return 1;
    }
}
