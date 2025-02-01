find_package(Boost 1.86.0 COMPONENTS filesystem)

add_executable(QTrading.Infra.Tests
  "Exanges/BinanceSimulator/DataProvider/MarketDataTest.cpp"
)

target_include_directories(QTrading.Infra.Tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(QTrading.Infra.Tests
    PRIVATE
        QTrading.Infra.Library         # 連結到剛剛建立的函式庫
        Boost::filesystem
        GTest::gtest_main  # 或 GTest::gtest + GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(QTrading.Infra.Tests)
