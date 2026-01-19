find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Signal.Tests
  BasisSignalEngineTests.cpp
)

target_include_directories(QTrading.Signal.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
)

target_link_libraries(QTrading.Signal.Tests
  PRIVATE
    QTrading.Signal.Library
    QTrading.Infra.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Signal.Tests)
