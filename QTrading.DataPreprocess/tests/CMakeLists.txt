add_executable(QTrading.DataPreprocess.Tests
  Aggregator/BinanceHourAggregatorTests.cpp
)

target_include_directories(QTrading.DataPreprocess.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.DataPreprocess.Tests
  PRIVATE
    QTrading.DataPreprocess.Library
    QTrading.Infra.Library
    Boost::filesystem
    Boost::thread
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.DataPreprocess.Tests)
