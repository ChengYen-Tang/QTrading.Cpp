project(QTrading.Monitoring LANGUAGES CXX)

add_library(QTrading.Monitoring.Library
  src/MonitoringModule.cpp
  src/SimpleMonitoring.cpp
)

target_include_directories(QTrading.Monitoring.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Monitoring.Library PUBLIC
  QTrading.Risk.Library
)

set_target_properties(QTrading.Monitoring.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
