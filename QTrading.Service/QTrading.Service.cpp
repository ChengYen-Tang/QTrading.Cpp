// QTrading.Service.cpp: 定義應用程式的進入點。
//

#include "QTrading.Service.h"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Aggregator/BinanceHourAggregator.hpp"
#include "Trend/UTBotStrategy.hpp"

#include <vector>
#include <memory>
#include <iostream>

using namespace std;

int main()
{
    // 1. Setup exchange simulator with symbol CSV mappings
    //    Update the paths below to point to your CSV data files
    std::vector<std::pair<std::string, std::string>> symbolCsv = {
        {"BTCUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\BTCUSDT.csv)"},
        //{"ETHUSDT", R"(\\Nas.kttw.xyz\docker\BinanceDataCollector\Data\Kline\UsdFutures\ETHUSDT.csv)"}
    };
    auto exchange = std::make_shared<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>(symbolCsv);

    auto aggregator = std::make_unique<QTrading::DataPreprocess::Aggregator::BinanceHourAggregator>(exchange, 10);

    auto strategy = std::make_unique<QTrading::Strategy::UTBotStrategy>(exchange, 1.0, 1.0, false);

    // 4. Wire the data flow: Exchange → Aggregator → Strategy → Exchange
    strategy->attach_in_channel(aggregator->get_market_channel());
    strategy->attach_exchange(exchange);

    // 5. Start background threads
    aggregator->start();
    strategy->start();

    // 6. Main simulation loop: advance exchange until data exhausted
    while (exchange->step()) {
        strategy->wait_for_done();
    }

    // 7. Shutdown: stop threads and close exchange channels
    aggregator->stop();
    strategy->stop();
    exchange->close();

    std::cout << "Simulation completed." << std::endl;
    return 0;
}
