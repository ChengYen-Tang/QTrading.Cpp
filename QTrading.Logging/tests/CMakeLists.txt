add_executable(QTrading.Logging.Tests
  "FileLogger/FeatherV2Tests.cpp"
)

target_include_directories(QTrading.Logging.Tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.Logging.Tests
  PRIVATE
    QTrading.Logging.Library
    Boost::thread
    Boost::filesystem
    Arrow::arrow_shared
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Logging.Tests)
