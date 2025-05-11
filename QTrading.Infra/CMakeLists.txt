project(QTrading.Infra LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS filesystem system)
find_package(GTest CONFIG REQUIRED)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
