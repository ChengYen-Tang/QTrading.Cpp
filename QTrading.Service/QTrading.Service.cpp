#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "ServiceHelpers.hpp"
#include "Execution/FundingCarryStrategy.hpp"
#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"
#include "Universe/FixedUniverseSelector.hpp"
#include "SinkLogger.hpp"
#include "FileLogger/FeatherV2Sink.hpp"
#include "Enum/LogModule.hpp"
#include "FileLogger/FeatherV2/AccountLog.hpp"
#include "FileLogger/FeatherV2/Order.hpp"
#include "FileLogger/FeatherV2/Position.hpp"
#include "FileLogger/FeatherV2/AccountEvent.hpp"
#include "FileLogger/FeatherV2/OrderEvent.hpp"
#include "FileLogger/FeatherV2/PositionEvent.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"
#include "FileLogger/FeatherV2/FundingEvent.hpp"
#include "FileLogger/FeatherV2/RunMetadata.hpp"
#include "Diagnostics/Trace.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace QTrading::Log;
using BinanceExchange = QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
using SimAccount = ::Account;

namespace {

#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
void PrintStatusLine(const BinanceExchange::StatusSnapshot& snap)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(2);
    oss << "[Service][Status] ts=" << snap.ts_exchange
        << " wallet=" << snap.wallet_balance
        << " margin=" << snap.margin_balance
        << " avail=" << snap.available_balance
        << " u_pnl=" << snap.total_unrealized_pnl
        << " spot_cash=" << snap.spot_cash_balance
        << " spot_inv=" << snap.spot_inventory_value
        << " spot_ledger=" << snap.spot_ledger_value
        << " perp_ledger=" << snap.perp_margin_balance
        << " total_ledger=" << snap.total_ledger_value
        << " progress=" << snap.progress_pct << "%";
    oss << " prices=";
    for (size_t i = 0; i < snap.prices.size(); ++i) {
        const auto& p = snap.prices[i];
        if (i > 0) {
            oss << ",";
        }
        oss << p.symbol << "=";
        if (p.has_price) {
            oss << p.price;
        }
        else {
            oss << "n/a";
        }
    }
    std::cout << oss.str() << std::endl;
}
#endif

} // namespace

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

        std::ostringstream strategy_params_builder;
        strategy_params_builder << "strategy_profile=funding_carry_default"
                                << ";initial_spot_cash=" << kInitialSpotCash
                                << ";initial_perp_wallet=" << kInitialPerpWallet;
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
                QTrading::Dto::Trading::InstrumentType::Spot},
            {"BTCUSDT_PERP",
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\Kline\UsdFutures\BTCUSDT.csv)"),
                QTrading::Service::Helpers::Utf8Path(u8R"(\\synology\MarketData\General\MarketData\FundingRate\UsdFutures\BTCUSDT.csv)"),
                QTrading::Dto::Trading::InstrumentType::Perp}
        };
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types;
        instrument_types.reserve(symbolCsv.size());
        for (const auto& ds : symbolCsv) {
            if (ds.instrument_type.has_value()) {
                instrument_types.emplace(ds.symbol, *ds.instrument_type);
            }
        }
        const uint64_t run_id = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        std::string dataset;
        for (const auto& ds : symbolCsv) {
            if (!dataset.empty()) {
                dataset += ";";
            }
            dataset += ds.symbol;
            dataset += "=";
            dataset += ds.kline_csv;
            dataset += "@";
            dataset += QTrading::Service::Helpers::InstrumentTypeToString(ds.instrument_type);
            if (ds.funding_csv.has_value() && !ds.funding_csv->empty()) {
                dataset += "|";
                dataset += *ds.funding_csv;
            }
        }

        std::filesystem::path logs_root = "logs";
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &now_time);
        std::ostringstream run_dir_name;
        run_dir_name << std::put_time(&tm, "%Y%m%d_%H%M%S");
        std::filesystem::path run_dir = logs_root / run_dir_name.str();
        std::filesystem::create_directories(run_dir);

        // @brief Shared logger writing Feather-V2 IPC files to logs/<timestamp>/.
        std::shared_ptr<SinkLogger> logger = std::make_shared<SinkLogger>(run_dir.string());
        logger->AddSink(std::make_unique<FileLogger::FeatherV2Sink>(run_dir.string()));
            {
                std::ofstream meta((run_dir / "run_metadata.json").string(), std::ios::out | std::ios::trunc);
                if (meta) {
                    meta << "{\n"
                         << "  \"run_id\": " << run_id << ",\n"
                         << "  \"strategy_name\": \"" << QTrading::Service::Helpers::JsonEscape("FundingCarryMVP") << "\",\n"
                         << "  \"strategy_version\": \"" << QTrading::Service::Helpers::JsonEscape("0.1") << "\",\n"
                         << "  \"strategy_params\": \"" << QTrading::Service::Helpers::JsonEscape(strategy_params) << "\",\n"
                         << "  \"dataset\": \"" << QTrading::Service::Helpers::JsonEscape(dataset) << "\"\n"
                         << "}\n";
                }
            }
            {
                std::ofstream dataset_file((run_dir / "dataset_paths.json").string(), std::ios::out | std::ios::trunc);
                if (dataset_file) {
                    dataset_file << "{\n"
                                 << "  \"dataset\": \"" << QTrading::Service::Helpers::JsonEscape(dataset) << "\"\n"
                                 << "}\n";
                }
            }
            logger->RegisterModule(LogModuleToString(LogModule::Account), FileLogger::FeatherV2::AccountLog::Schema, FileLogger::FeatherV2::AccountLog::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::Position), FileLogger::FeatherV2::Position::Schema, FileLogger::FeatherV2::Position::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::Order), FileLogger::FeatherV2::Order::Schema, FileLogger::FeatherV2::Order::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::AccountEvent), FileLogger::FeatherV2::AccountEvent::Schema(), FileLogger::FeatherV2::AccountEvent::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::PositionEvent), FileLogger::FeatherV2::PositionEvent::Schema(), FileLogger::FeatherV2::PositionEvent::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::OrderEvent), FileLogger::FeatherV2::OrderEvent::Schema(), FileLogger::FeatherV2::OrderEvent::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::MarketEvent), FileLogger::FeatherV2::MarketEvent::Schema(), FileLogger::FeatherV2::MarketEvent::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::FundingEvent), FileLogger::FeatherV2::FundingEvent::Schema(), FileLogger::FeatherV2::FundingEvent::Serializer);
            logger->RegisterModule(LogModuleToString(LogModule::RunMetadata), FileLogger::FeatherV2::RunMetadata::Schema(), FileLogger::FeatherV2::RunMetadata::Serializer);
            logger->Start();

            {
                FileLogger::FeatherV2::RunMetadataDto meta{};
                meta.run_id = run_id;
                meta.strategy_name = "FundingCarryMVP";
                meta.strategy_version = "0.1";
                meta.strategy_params = strategy_params;
                meta.dataset = dataset;
                logger->Log(LogModuleToString(LogModule::RunMetadata), std::move(meta));
        }

        std::cerr << "[Service] constructing exchange..." << std::endl;
        std::cerr.flush();
        // @brief Exchange simulator providing 1-minute MultiKlineDto.
        auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(
            symbolCsv, logger, account_init, run_id);
        std::cerr << "[Service] exchange constructed" << std::endl;
        std::cerr.flush();

        // @brief Assemble arbitrage pipeline (currently null implementations).
        QTrading::Universe::FixedUniverseSelector universe_selector;
        QTrading::Signal::FundingCarrySignalEngine signal_engine({});
        QTrading::Intent::FundingCarryIntentBuilder intent_builder({});
        QTrading::Risk::SimpleRiskEngine::Config risk_cfg;
        risk_cfg.instrument_types = instrument_types;
        QTrading::Risk::SimpleRiskEngine risk_engine(risk_cfg);
        QTrading::Execution::MarketExecutionEngine execution_engine(exchange, {});
        QTrading::Monitoring::SimpleMonitoring monitoring({});
        auto strategy = std::make_shared<QTrading::Execution::FundingCarryStrategy>(
            exchange,
            universe_selector,
            signal_engine,
            intent_builder,
            risk_engine,
            execution_engine,
            monitoring,
            instrument_types);

        std::cerr << "[Service] entering main loop..." << std::endl;
        std::cerr.flush();

        uint64_t steps = 0;
        auto last_progress = std::chrono::steady_clock::now();
        bool stop_logged = false;
        bool shutdown_initiated = false;
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
        BinanceExchange::StatusSnapshot status;
#endif
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
            strategy->wait_for_done();

            ++steps;

            // Lightweight progress heartbeat every ~2s to help spot freezes.
            auto now = std::chrono::steady_clock::now();
            if (now - last_progress >= std::chrono::seconds(2)) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
                exchange->FillStatusSnapshot(status);
                PrintStatusLine(status);
#else
                std::cout << "[Service] steps=" << steps
                          << std::endl;
#endif
                last_progress = now;
            }
        }
        if (!QTrading::Service::Helpers::StopRequested()) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
            exchange->FillStatusSnapshot(status);
            PrintStatusLine(status);
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
