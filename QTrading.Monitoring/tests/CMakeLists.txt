find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Monitoring.Tests
  SimpleMonitoringTests.cpp
)

target_include_directories(QTrading.Monitoring.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.Monitoring.Tests
  PRIVATE
    QTrading.Monitoring.Library
    QTrading.Risk.Library
    QTrading.Infra.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Monitoring.Tests)
