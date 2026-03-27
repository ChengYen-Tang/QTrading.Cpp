add_executable(QTrading.Infra.Tests
  Exchanges/BinanceSimulator/BinanceExchangeReplayTests.cpp
)

target_include_directories(QTrading.Infra.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Logging/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
)

target_link_libraries(QTrading.Infra.Tests
  PRIVATE
    QTrading.Infra.Library
    QTrading.Logging.Library
    Boost::filesystem
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Infra.Tests)

string(CONCAT QTRADING_INFRA_REPLAY_SEMANTICS_GATE_FILTER
  "BinanceExchangeFixture.SymbolsSynchronisedWithHoles:"
  "BinanceExchangeFixture.StepSuccessPublishesMarketChannel:"
  "BinanceExchangeFixture.ReplayExhaustedClosesAllPublicChannels:"
  "BinanceExchangeFixture.ReplayExhaustedKeepsChannelsClosedOnSubsequentSteps:"
  "BinanceExchangeFixture.StatusSnapshotCarriesLatestTradePriceContext:"
  "BinanceExchangeFixture.StatusSnapshotExposesMinimalDualLedgerBalances:"
  "BinanceExchangeFixture.StatusSnapshotTracksReplayProgressAndTimestamp")

add_test(
  NAME QTrading.Infra.ReplaySemanticsGate
  COMMAND $<TARGET_FILE:QTrading.Infra.Tests> --gtest_filter=${QTRADING_INFRA_REPLAY_SEMANTICS_GATE_FILTER}
)
set_tests_properties(QTrading.Infra.ReplaySemanticsGate PROPERTIES
  LABELS "REPLAY_SEMANTICS_GATE"
)
