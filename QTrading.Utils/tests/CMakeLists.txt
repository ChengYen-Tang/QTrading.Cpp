add_executable(QTrading.Utils.Tests
  "Queue/BoundedChannelTest.cpp"
 )

target_include_directories(QTrading.Utils.Tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.Utils.Tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main  # 或 GTest::gtest + GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Utils.Tests)
