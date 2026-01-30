find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Intent.Tests
  BasisIntentBuilderTests.cpp
  FundingCarryIntentBuilderTests.cpp
)

target_include_directories(QTrading.Intent.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.Intent.Tests
  PRIVATE
    QTrading.Intent.Library
    QTrading.Signal.Library
    QTrading.Infra.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Intent.Tests)
