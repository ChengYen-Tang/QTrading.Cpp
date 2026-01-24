#include "QTrading.Service.h"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Execution/MarketExecutionEngine.hpp"
#include "Intent/BasisIntentBuilder.hpp"
#include "Monitoring/SimpleMonitoring.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Signal/BasisSignalEngine.hpp"
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
#include "Dto/Trading/Side.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
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
using MarketPtr = QTrading::Infra::Exchanges::BinanceSim::MultiKlinePtr;

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

std::string Utf8Path(const char8_t* path)
{
    return std::string(reinterpret_cast<const char*>(path));
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

QTrading::Dto::Trading::OrderSide ToOrderSide(QTrading::Execution::OrderAction action)
{
    return (action == QTrading::Execution::OrderAction::Buy)
        ? QTrading::Dto::Trading::OrderSide::Buy
        : QTrading::Dto::Trading::OrderSide::Sell;
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
        std::vector<BinanceExchange::SymbolDataset> symbolCsv = {
            {"BTCUSDT_SPOT", Utf8Path(u8R"(\\synology\MarketData\Kline\Spot\BTCUSDT.csv)"), std::nullopt},
            {"BTCUSDT_PERP", Utf8Path(u8R"(\\synology\MarketData\Kline\UsdFutures\BTCUSDT.csv)"), std::nullopt}
        };
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
            if (ds.funding_csv.has_value() && !ds.funding_csv->empty()) {
                dataset += "|";
                dataset += *ds.funding_csv;
            }
        }

        // @brief Shared logger writing Feather-V2 IPC files to `logs/`.
        {
            std::error_code ec;
            std::filesystem::remove_all("logs", ec);
        }
        std::shared_ptr<SinkLogger> logger = std::make_shared<SinkLogger>("logs");
        logger->AddSink(std::make_unique<FileLogger::FeatherV2Sink>("logs"));
        {
            std::ofstream meta("logs/run_metadata.json", std::ios::out | std::ios::trunc);
            if (meta) {
                meta << "{\n"
                     << "  \"run_id\": " << run_id << ",\n"
                     << "  \"strategy_name\": \"" << JsonEscape("BasisArbMVP") << "\",\n"
                     << "  \"strategy_version\": \"" << JsonEscape("0.1") << "\",\n"
                     << "  \"strategy_params\": \"" << JsonEscape("notional=1000;leverage=2") << "\",\n"
                     << "  \"dataset\": \"" << JsonEscape(dataset) << "\"\n"
                     << "}\n";
            }
        }
        {
            std::ofstream dataset_file("logs/dataset_paths.json", std::ios::out | std::ios::trunc);
            if (dataset_file) {
                dataset_file << "{\n"
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
        logger->RegisterModule(LogModuleToString(LogModule::FundingEvent), FileLogger::FeatherV2::FundingEvent::Schema(), FileLogger::FeatherV2::FundingEvent::Serializer);
        logger->RegisterModule(LogModuleToString(LogModule::RunMetadata), FileLogger::FeatherV2::RunMetadata::Schema(), FileLogger::FeatherV2::RunMetadata::Serializer);
        logger->Start();

        {
            FileLogger::FeatherV2::RunMetadataDto meta{};
            meta.run_id = run_id;
            meta.strategy_name = "BasisArbMVP";
            meta.strategy_version = "0.1";
            meta.strategy_params = "notional=1000;leverage=2";
            meta.dataset = dataset;
            logger->Log(LogModuleToString(LogModule::RunMetadata), std::move(meta));
        }

        std::cerr << "[Service] constructing exchange..." << std::endl;
        std::cerr.flush();
        // @brief Exchange simulator providing 1-minute MultiKlineDto.
        auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(symbolCsv, logger, 1'000'000.0, 0, run_id);
        std::cerr << "[Service] exchange constructed" << std::endl;
        std::cerr.flush();

        // @brief Assemble arbitrage pipeline (currently null implementations).
        QTrading::Universe::FixedUniverseSelector universe_selector({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
        QTrading::Signal::BasisSignalEngine signal_engine({
            "BTCUSDT_SPOT",
            "BTCUSDT_PERP",
            120,
            2.0,
            0.5
        });
        QTrading::Intent::BasisIntentBuilder intent_builder({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
        QTrading::Risk::SimpleRiskEngine risk_engine({ 1000.0, 2.0, 3.0 });
        QTrading::Execution::MarketExecutionEngine execution_engine(exchange, { 10.0 });
        QTrading::Monitoring::SimpleMonitoring monitoring({ 5 });

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
            if (!ok) {
#if defined(QTRADING_TRACE) && !defined(QTRADING_TRACE_VERBOSE)
                exchange->FillStatusSnapshot(status);
                PrintStatusLine(status);
#else
                std::cerr << "[Service] exchange->step returned false; exiting loop." << std::endl;
#endif
                break;
            }
            if (StopRequested()) {
                if (!stop_logged) {
                    stop_logged = true;
                }
                Shutdown("stop requested, shutting down modules...");
                break;
            }

            auto marketOpt = exchange->get_market_channel()->Receive();
            if (!marketOpt) {
                std::cerr << "[Service] market channel closed; exiting loop." << std::endl;
                break;
            }
            const auto& market = marketOpt.value();

            (void)universe_selector.select();
            auto signal = signal_engine.on_market(market);
            auto intent = intent_builder.build(signal, market);

            QTrading::Risk::AccountState account{};
            account.positions = exchange->get_all_positions();
            account.open_orders = exchange->get_all_open_orders();

            auto risk = risk_engine.position(intent, account, market);
            auto orders = execution_engine.plan(risk, signal, market);

            for (const auto& order : orders) {
                double price = (order.type == QTrading::Execution::OrderType::Limit)
                    ? order.price
                    : 0.0;
                (void)exchange->place_order(order.symbol, order.qty, price, ToOrderSide(order.action),
                    QTrading::Dto::Trading::PositionSide::Both, order.reduce_only);
            }

            for (const auto& alert : monitoring.check(account)) {
                if (alert.action == "CANCEL_OPEN_ORDERS") {
                    exchange->cancel_open_orders(alert.symbol);
                }
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
                          << std::endl;
#endif
                last_progress = now;
            }
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
