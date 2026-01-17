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

if(QTRADING_ENABLE_TRACE)
  target_compile_definitions(QTrading.DataPreprocess.Library PRIVATE QTRADING_TRACE)
  if(QTRADING_TRACE_VERBOSE)
    target_compile_definitions(QTrading.DataPreprocess.Library PRIVATE QTRADING_TRACE_VERBOSE)
  endif()
endif()

target_link_libraries(QTrading.DataPreprocess.Library
  PRIVATE
    Boost::thread
)
