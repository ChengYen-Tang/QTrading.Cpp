project(QTrading.Risk LANGUAGES CXX)

add_library(QTrading.Risk.Library
  src/RiskModule.cpp
  src/SimpleRiskEngine.cpp
)

target_include_directories(QTrading.Risk.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Risk.Library PUBLIC
  QTrading.Infra.Library
  QTrading.Intent.Library
)

set_target_properties(QTrading.Risk.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
