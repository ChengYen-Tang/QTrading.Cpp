find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Execution.Tests
  MarketExecutionEngineTests.cpp
)

target_include_directories(QTrading.Execution.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.Execution.Tests
  PRIVATE
    QTrading.Execution.Library
    QTrading.Risk.Library
    QTrading.Signal.Library
    QTrading.Infra.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Execution.Tests)
