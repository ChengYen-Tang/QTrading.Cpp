find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Risk.Tests
  SimpleRiskEngineTests.cpp
)

target_include_directories(QTrading.Risk.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.Risk.Tests
  PRIVATE
    QTrading.Risk.Library
    QTrading.Intent.Library
    QTrading.Infra.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Risk.Tests)
