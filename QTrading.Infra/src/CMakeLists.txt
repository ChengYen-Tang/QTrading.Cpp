add_library(QTrading.Infra.Library STATIC
  Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.cpp
  Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.cpp
  Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.cpp
  Exchanges/BinanceSimulator/BinanceExchange.cpp
  Exchanges/BinanceSimulator/Api/SpotApi.cpp
  Exchanges/BinanceSimulator/Api/PerpApi.cpp
  Exchanges/BinanceSimulator/Api/AccountApi.cpp
)

target_include_directories(QTrading.Infra.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Logging/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
)

if(QTRADING_ENABLE_TRACE)
  target_compile_definitions(QTrading.Infra.Library PRIVATE QTRADING_TRACE)
  if(QTRADING_TRACE_VERBOSE)
    target_compile_definitions(QTrading.Infra.Library PRIVATE QTRADING_TRACE_VERBOSE)
  endif()
endif()

target_link_libraries(QTrading.Infra.Library
  PUBLIC
    QTrading.Logging.Library
  PRIVATE
    Boost::filesystem
    Boost::system
)
