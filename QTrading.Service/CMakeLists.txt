project(QTrading.Service LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS thread)

add_executable(QTrading.Service
  DatasetUniverseConfig.cpp
  DatasetUniverseConfig.hpp
  QTrading.Service.cpp
  ServiceHelpers.cpp
  QTrading.Service.h
  ServiceHelpers.hpp
)

target_include_directories(QTrading.Service PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Logging/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.Infra/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.Strategy/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.Utils/include
)

if(QTRADING_ENABLE_TRACE)
  target_compile_definitions(QTrading.Service PRIVATE QTRADING_TRACE)
  if(QTRADING_TRACE_VERBOSE)
    target_compile_definitions(QTrading.Service PRIVATE QTRADING_TRACE_VERBOSE)
  endif()
endif()

target_link_libraries(QTrading.Service
  PRIVATE
    QTrading.Logging.Library
    QTrading.Infra.Library
    QTrading.Strategy.Library
    Boost::thread
)

set_target_properties(QTrading.Service PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
