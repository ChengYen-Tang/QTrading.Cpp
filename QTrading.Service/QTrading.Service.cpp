#include "QTrading.Service.h"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Aggregator/BinanceHourAggregator.hpp"
#include "Trend/UTBotStrategy.hpp"
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
#include "FileLogger/FeatherV2/RunMetadata.hpp"
#include "Diagnostics/Trace.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace QTrading::Log;
using BinanceExchange = QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

namespace {

std::string JsonEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        if (c == '\\') {
            out += "\\\\";
        }
        else if (c == '"') {
            out += "\\\"";
        }
        else {
            out.push_back(c);
        }
    }
    return out;
}

std::atomic<bool> g_stop_requested{ false };

void HandleSignal(int)
{
    g_stop_requested.store(true, std::memory_order_relaxed);
}

bool StopRequested()
{
    return g_stop_requested.load(std::memory_order_relaxed);
}

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
/// @brief Defines the application entry point and wires together exchange, aggregator, strategy, and logger.

/// @brief Main entry point for QTrading simulation.
/// @details 
///   1. Configure CSV input for each symbol  
///   2. Set up FeatherV2 logger and register modules  
///   3. Instantiate exchange simulator, hourly aggregator, and UT-Bot strategy  
///   4. Wire data flow: Exchange → Aggregator → Strategy → Exchange  
///   5. Start background threads  
///   6. Run simulation loop until market data is exhausted  
///   7. Perform clean shutdown  
/// @return Returns 0 on successful completion.
int main()
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

#ifdef QTRADING_TRACE
    std::cerr << "[Service] QTRADING_TRACE enabled" << std::endl;
#else
    std::cerr << "[Service] QTRADING_TRACE disabled (define QTRADING_TRACE to enable trace logs)" << std::endl;
#endif
    std::cerr.flush();
    QTR_TRACE("service", "main begin");

    try {
        /// @brief Mapping from symbol string to CSV file path.
        std::vector<std::pair<std::string, std::string>> symbolCsv = {
            {"BTCUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\BTCUSDT.csv)"}
            //{"ETHUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\ETHUSDT.csv)"}
        };
        const uint64_t run_id = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        std::string dataset;
        for (const auto& [sym, csv] : symbolCsv) {
            if (!dataset.empty()) {
                dataset += ";";
            }
            dataset += sym;
            dataset += "=";
            dataset += csv;
        }

        // @brief Shared logger writing Feather-V2 IPC files to `logs/`.
        std::shared_ptr<SinkLogger> logger = std::make_shared<SinkLogger>("logs");
        logger->AddSink(std::make_unique<FileLogger::FeatherV2Sink>("logs"));
        {
            std::ofstream meta("logs/run_metadata.json", std::ios::out | std::ios::trunc);
            if (meta) {
                meta << "{\n"
                     << "  \"run_id\": " << run_id << ",\n"
                     << "  \"strategy_name\": \"" << JsonEscape("UTBotStrategy") << "\",\n"
                     << "  \"strategy_version\": \"" << JsonEscape("1.0") << "\",\n"
                     << "  \"strategy_params\": \"" << JsonEscape("atr=1.0;multiplier=1.0;use_trailing=false") << "\",\n"
                     << "  \"dataset\": \"" << JsonEscape(dataset) << "\"\n"
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
        logger->RegisterModule(LogModuleToString(LogModule::RunMetadata), FileLogger::FeatherV2::RunMetadata::Schema(), FileLogger::FeatherV2::RunMetadata::Serializer);
        logger->Start();

        {
            FileLogger::FeatherV2::RunMetadataDto meta{};
            meta.run_id = run_id;
            meta.strategy_name = "UTBotStrategy";
            meta.strategy_version = "1.0";
            meta.strategy_params = "atr=1.0;multiplier=1.0;use_trailing=false";
            meta.dataset = dataset;
            logger->Log(LogModuleToString(LogModule::RunMetadata), std::move(meta));
        }

        std::cerr << "[Service] constructing exchange..." << std::endl;
        std::cerr.flush();
        // @brief Exchange simulator providing 1-minute MultiKlineDto.
        auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(symbolCsv, logger, 1'000'000.0, 0, run_id);
        std::cerr << "[Service] exchange constructed" << std::endl;
        std::cerr.flush();

        // @brief Aggregator that builds 1-hour bars and keeps last N hours.
        auto aggregator = std::make_unique<QTrading::DataPreprocess::Aggregator::BinanceHourAggregator>(exchange, 10);

        // @brief UT-Bot strategy consuming hourly bars to generate orders.
        auto strategy = std::make_unique<QTrading::Strategy::UTBotStrategy>(exchange, 1.0, 1.0, false);

        // @brief Wire the pipeline: Exchange → Aggregator → Strategy.
        strategy->attach_in_channel(aggregator->get_market_channel());
        strategy->attach_exchange(exchange);

        std::cerr << "[Service] starting threads..." << std::endl;
        std::cerr.flush();
        // @brief Start background threads for aggregator and strategy.
        aggregator->start();
        strategy->start();

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
            aggregator->stop();
            strategy->stop();
            exchange->close();
        };

        // @brief Main simulation loop: advance exchange until no more data.
        while (true) {
            if (StopRequested()) {
                if (!stop_logged) {
                    stop_logged = true;
                }
                Shutdown("stop requested, shutting down modules...");
                break;
            }
            QTR_TRACE("service", "exchange->step begin");
            const bool ok = exchange->step();
            QTR_TRACE("service", ok ? "exchange->step end ok" : "exchange->step end false");
            if (!ok) break;
            if (StopRequested()) {
                if (!stop_logged) {
                    stop_logged = true;
                }
                Shutdown("stop requested, shutting down modules...");
                break;
            }

            if (!strategy->wait_for_done_for(std::chrono::seconds(5))) {
                std::cerr << "[Service][WARN] wait_for_done timeout. steps=" << steps
                          << " ex_market_closed=" << exchange->get_market_channel()->IsClosed()
                          << " agg_closed=" << aggregator->get_market_channel()->IsClosed()
                          << std::endl;
                break;
            }

            ++steps;

            // Lightweight progress heartbeat every ~2s to help spot freezes.
            auto now = std::chrono::steady_clock::now();
            if (now - last_progress >= std::chrono::seconds(2)) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
                exchange->FillStatusSnapshot(status);
                PrintStatusLine(status);
#else
                std::cout << "[Service] steps=" << steps
                          << " ex_market_closed=" << exchange->get_market_channel()->IsClosed()
                          << " agg_closed=" << aggregator->get_market_channel()->IsClosed()
                          << std::endl;
#endif
                last_progress = now;
            }
        }

        // @brief Clean shutdown: stop threads, close channels, stop logger.
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
