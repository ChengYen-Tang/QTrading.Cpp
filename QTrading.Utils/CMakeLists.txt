project(QTrading.Utils LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(QTrading.Utils.Library STATIC
  src/Calibration/CalibrationMetrics.cpp
)

target_include_directories(QTrading.Utils.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()
add_subdirectory(tests)
