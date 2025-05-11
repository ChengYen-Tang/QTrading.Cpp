project(QTrading.Utils LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED)      # 如果只是头文件，可依需求删掉
find_package(GTest CONFIG REQUIRED)

enable_testing()
add_subdirectory(tests)
