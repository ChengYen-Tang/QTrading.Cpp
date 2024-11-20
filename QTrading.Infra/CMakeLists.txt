# CMakeList.txt: QTrading.Service 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。

find_package(Boost 1.86.0 COMPONENTS filesystem)
if(Boost_FOUND)
include_directories(${Boost_INCLUDE_DIRS})
message(Boost_INCLUDE_DIRS="${Boost_INCLUDE_DIRS}")
link_directories(${Boost_LIBRARY_DIRS})
message(Boost_LIBRARY_DIRS="${Boost_LIBRARY_DIRS}")

SET(header
  "exanges/binance_simulator/futures/config.h",
  "exanges/binance_simulator/futures/account.h",
  "exanges/binance_simulator/data_provider/market_data.h"
)
SET(source
"exanges/binance_simulator/futures/account.cpp",
  "exanges/binance_simulator/data_provider/market_data.cpp"
)

add_library (QTrading.Infra ${source})
target_link_libraries(QTrading.Infra Boost::filesystem)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET QTrading.Infra PROPERTY CXX_STANDARD 20)
endif()
endif()
