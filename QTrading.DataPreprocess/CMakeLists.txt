project(QTrading.DataPreprocess LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS thread filesystem)
find_package(GTest CONFIG REQUIRED)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
