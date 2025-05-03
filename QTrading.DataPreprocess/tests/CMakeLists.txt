find_package(Boost 1.86.0 COMPONENTS filesystem)

add_executable(QTrading.DataPreprocess.Tests
  "Aggregator/BinanceHourAggregatorTests.cpp")

target_include_directories(QTrading.DataPreprocess.Tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.DataPreprocess.Tests
    PRIVATE
        QTrading.Infra.Library
        QTrading.DataPreprocess.Library         # 連結到剛剛建立的函式庫
        ${Boost_LIBRARIES}
        Boost::filesystem
        Boost::thread
        GTest::gtest
        GTest::gtest_main  # 或 GTest::gtest + GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.DataPreprocess.Tests)
