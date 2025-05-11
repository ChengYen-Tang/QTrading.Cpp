project(QTrading.Strategy LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS thread)

add_subdirectory(src)
