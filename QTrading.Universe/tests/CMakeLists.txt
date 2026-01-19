find_package(GTest CONFIG REQUIRED)

add_executable(QTrading.Universe.Tests
  UniverseTests.cpp
)

target_include_directories(QTrading.Universe.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.Universe.Tests
  PRIVATE
    QTrading.Universe.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Universe.Tests)
