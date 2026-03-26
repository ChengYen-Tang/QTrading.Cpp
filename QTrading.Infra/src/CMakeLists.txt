add_library(QTrading.Infra.Library STATIC
  Data/Binance/FundingRateData.cpp
  Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.cpp
  Data/Binance/MarketData.cpp
  Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.cpp
  Exchanges/BinanceSimulator/Application/MarketReplayKernel.cpp
  Exchanges/BinanceSimulator/Application/OrderCommandKernel.cpp
  Exchanges/BinanceSimulator/Application/TerminationPolicy.cpp
  Exchanges/BinanceSimulator/Application/StepKernel.cpp
  Exchanges/BinanceSimulator/Domain/BinanceRejectSurface.cpp
  Exchanges/BinanceSimulator/Domain/FillSettlementEngine.cpp
  Exchanges/BinanceSimulator/Domain/LiquidationEligibilityDecision.cpp
  Exchanges/BinanceSimulator/Domain/LiquidationExecution.cpp
  Exchanges/BinanceSimulator/Domain/MatchingEngine.cpp
  Exchanges/BinanceSimulator/Domain/OrderEntryService.cpp
  Exchanges/BinanceSimulator/Domain/ReferencePriceResolver.cpp
  Exchanges/BinanceSimulator/Output/ChannelPublisher.cpp
  Exchanges/BinanceSimulator/Output/SnapshotBuilder.cpp
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
