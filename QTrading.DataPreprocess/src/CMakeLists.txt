add_library(QTrading.DataPreprocess.Library STATIC
  Simulator/Binance.cpp
  Aggregator/BinanceHourAggregator.cpp
)

target_include_directories(QTrading.DataPreprocess.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.DataPreprocess.Library
  PRIVATE
    Boost::thread
)
