#include "QTrading.Service.h"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Aggregator/BinanceHourAggregator.hpp"
#include "Trend/UTBotStrategy.hpp"
#include "FileLogger/FeatherV2.hpp"
#include "Enum/LogModule.hpp"
#include "FileLogger/FeatherV2/AccountLog.hpp"
#include "FileLogger/FeatherV2/Order.hpp"
#include "FileLogger/FeatherV2/Position.hpp"

#include <vector>
#include <memory>
#include <iostream>

using namespace std;
using namespace QTrading::Log;

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
    /// @brief Mapping from symbol string to CSV file path.
    std::vector<std::pair<std::string, std::string>> symbolCsv = {
        {"BTCUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\BTCUSDT.csv)"}
        //{"ETHUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\ETHUSDT.csv)"}
    };

    // @brief Shared logger writing Feather-V2 IPC files to `logs/`.
    std::shared_ptr<FeatherV2> logger = std::make_shared<FeatherV2>("logs");
    logger->RegisterModule(LogModuleToString(LogModule::Account), FileLogger::FeatherV2::AccountLog::Schema, FileLogger::FeatherV2::AccountLog::Serializer);
    logger->RegisterModule(LogModuleToString(LogModule::Position), FileLogger::FeatherV2::Position::Schema, FileLogger::FeatherV2::Position::Serializer);
    logger->RegisterModule(LogModuleToString(LogModule::Order), FileLogger::FeatherV2::Order::Schema, FileLogger::FeatherV2::Order::Serializer);
    logger->Start();

    // @brief Exchange simulator providing 1-minute MultiKlineDto.
    auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(symbolCsv, logger);

    // @brief Aggregator that builds 1-hour bars and keeps last N hours.
    auto aggregator = std::make_unique<QTrading::DataPreprocess::Aggregator::BinanceHourAggregator>(exchange, 10);

    // @brief UT-Bot strategy consuming hourly bars to generate orders.
    auto strategy = std::make_unique<QTrading::Strategy::UTBotStrategy>(exchange, 1.0, 1.0, false);

    // @brief Wire the pipeline: Exchange → Aggregator → Strategy.
    strategy->attach_in_channel(aggregator->get_market_channel());
    strategy->attach_exchange(exchange);

    // @brief Start background threads for aggregator and strategy.
    aggregator->start();
    strategy->start();

    // @brief Main simulation loop: advance exchange until no more data.
    while (exchange->step()) {
        strategy->wait_for_done();
    }

    // @brief Clean shutdown: stop threads, close channels, stop logger.
    aggregator->stop();
    strategy->stop();
    exchange->close();
    logger->Stop();

    std::cout << "Simulation completed." << std::endl;
    return 0;
}
