add_library(QTrading.Infra.Library STATIC
  Exchanges/BinanceSimulator/Account/Account.cpp
  Exchanges/BinanceSimulator/Account/AccountCoreV2.cpp
  Exchanges/BinanceSimulator/Account/Account.IndexCache.cpp
  Exchanges/BinanceSimulator/Account/Account.Opening.cpp
  Exchanges/BinanceSimulator/Account/Account.UpdatePipeline.cpp
  Exchanges/BinanceSimulator/Account/AccountPolicies.cpp
  Exchanges/BinanceSimulator/Spot/Account.SpotLedger.cpp
  Exchanges/BinanceSimulator/Spot/Account.SpotPlacement.cpp
  Exchanges/BinanceSimulator/Spot/Account.SpotCloseCancel.cpp
  Exchanges/BinanceSimulator/Spot/Account.SpotOrderFlow.cpp
  Exchanges/BinanceSimulator/Spot/Account.SpotApi.cpp
  Exchanges/BinanceSimulator/Spot/SpotLedgerEngine.cpp
  Exchanges/BinanceSimulator/Spot/BinanceExchange.SpotApi.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpLedger.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpPlacement.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpCloseCancel.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpClosePlacement.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpOrderFlow.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpLiquidation.cpp
  Exchanges/BinanceSimulator/Perp/Account.PerpApi.cpp
  Exchanges/BinanceSimulator/Perp/PerpLedgerEngine.cpp
  Exchanges/BinanceSimulator/Perp/BinanceExchange.PerpApi.cpp
  Exchanges/BinanceSimulator/Domain/BinanceRejectSurface.cpp
  Exchanges/BinanceSimulator/Domain/OrderEntryService.cpp
  Data/Binance/MarketData.cpp
  Data/Binance/FundingRateData.cpp
  Exchanges/BinanceSimulator/Application/LegacyStepBackend.cpp
  Exchanges/BinanceSimulator/Application/StepKernel.cpp
  Exchanges/BinanceSimulator/Output/EventEnvelopePublisher.cpp
  Exchanges/BinanceSimulator/Output/LegacyEventEnvelopeEmitter.cpp
  Exchanges/BinanceSimulator/Output/PositionOrderSnapshotGate.cpp
  Exchanges/BinanceSimulator/Output/SideEffectStepNotifier.cpp
  Exchanges/BinanceSimulator/Output/StatusSnapshotBuilder.cpp
  Exchanges/BinanceSimulator/BinanceExchange.cpp
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
