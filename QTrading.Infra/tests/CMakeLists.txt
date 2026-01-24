add_executable(QTrading.Infra.Tests
  Exchanges/BinanceSimulator/DataProvider/MarketDataTests.cpp
  Exchanges/BinanceSimulator/DataProvider/FundingRateDataTests.cpp
  Exchanges/BinanceSimulator/Futures/AccountTests.cpp
  Exchanges/BinanceSimulator/Futures/AccountPolicyInjectionTests.cpp
  Exchanges/BinanceSimulator/BinanceExchangeTests.cpp
)

target_include_directories(QTrading.Infra.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Logging/include
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
