# CMakeList.txt: QTrading.Service 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。
project(QTrading.Infra)
find_package(Boost 1.86.0 COMPONENTS filesystem)
if(Boost_FOUND)
include_directories(${Boost_INCLUDE_DIRS})
message(Boost_INCLUDE_DIRS="${Boost_INCLUDE_DIRS}")
link_directories(${Boost_LIBRARY_DIRS})
message(Boost_LIBRARY_DIRS="${Boost_LIBRARY_DIRS}")


SET(SOURCE_FILES
  "src/Exanges/BinanceSimulator/Futures/Account.cpp"
  "src/Exanges/BinanceSimulator/DataProvider/MarketData.cpp"
)

add_library (QTrading.Infra SHARED ${SOURCE_FILES})
target_include_directories(QTrading.Infra PUBLIC include)

target_link_libraries(QTrading.Infra Boost::filesystem)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET QTrading.Infra PROPERTY CXX_STANDARD 20)
endif()
endif()
