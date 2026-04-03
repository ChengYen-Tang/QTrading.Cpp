add_executable(QTrading.Utils.Tests
  Calibration/CalibrationMetricsTests.cpp
  Queue/BoundedChannelTests.cpp
  Queue/UnboundedChannelTests.cpp
  Time/ReplayTimeRangeTests.cpp
)

target_include_directories(QTrading.Utils.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.Utils.Tests
  PRIVATE
    QTrading.Utils.Library
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Utils.Tests)

