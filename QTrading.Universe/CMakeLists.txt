project(QTrading.Universe LANGUAGES CXX)

add_library(QTrading.Universe.Library
  src/UniverseModule.cpp
)

target_include_directories(QTrading.Universe.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(QTrading.Universe.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
