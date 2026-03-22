add_executable(QTrading.Infra.Tests
  InfraLogFeatherRoundTripTests.cpp
  InfraLogTests.cpp
  Exchanges/BinanceSimulator/DataProvider/MarketDataTests.cpp
  Exchanges/BinanceSimulator/DataProvider/FundingRateDataTests.cpp
  Exchanges/BinanceSimulator/Account/AccountTests.cpp
  Exchanges/BinanceSimulator/Account/AccountCoreV2Tests.cpp
  Exchanges/BinanceSimulator/Account/AccountServicesPhase3Tests.cpp
  Exchanges/BinanceSimulator/Account/AccountPolicyInjectionTests.cpp
  Exchanges/BinanceSimulator/BinanceExchangeLogTests.cpp
  Exchanges/BinanceSimulator/BinanceExchangeTests.cpp
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

string(CONCAT QTRADING_INFRA_BASELINE_SEMANTICS_GATE_FILTER
  "InfraLogTestFixture.StatusSnapshotsAreEmittedBeforeSameStepEvents:"
  "InfraLogTestFixture.EventModulesPreserveMarketFundingAccountPositionOrderOrdering:"
  "InfraLogTestFixture.AsyncAckPendingResolvedAndRejectMappingStayStable:"
  "InfraLogTestFixture.FundingAndFillSameStepBeforeAfterMatchingRemainStable:"
  "InfraLogTestFixture.LiquidationSyntheticFillContractRemainsVisible:"
  "BinanceExchangeFixture.BalanceDepletedTerminationClosesAllPublicChannels")

add_test(
  NAME QTrading.Infra.BaselineSemanticsGate
  COMMAND $<TARGET_FILE:QTrading.Infra.Tests> --gtest_filter=${QTRADING_INFRA_BASELINE_SEMANTICS_GATE_FILTER}
)
set_tests_properties(QTrading.Infra.BaselineSemanticsGate PROPERTIES
  LABELS "BASELINE_SEMANTICS_GATE"
)
