# CMakeList.txt: QTrading.Utils 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。
project(QTrading.Utils)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME ON)
find_package(Boost 1.86.0 REQUIRED)
if(Boost_FOUND)
include_directories(${Boost_INCLUDE_DIRS})
message(Boost_INCLUDE_DIRS="${Boost_INCLUDE_DIRS}")
link_directories(${Boost_LIBRARY_DIRS})
message(Boost_LIBRARY_DIRS="${Boost_LIBRARY_DIRS}")

#add_subdirectory ("src")
enable_testing()
add_subdirectory ("tests")

endif()
