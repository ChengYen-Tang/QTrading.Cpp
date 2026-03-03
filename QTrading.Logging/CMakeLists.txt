# CMakeList.txt: QTrading.Logging 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。
project(QTrading.Logging LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS thread filesystem)
find_package(Arrow CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
