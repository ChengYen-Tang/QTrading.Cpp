project(QTrading.Execution LANGUAGES CXX)

add_library(QTrading.Execution.Library
  src/ExecutionModule.cpp
  src/FundingCarryExecutionOrchestrator.cpp
  src/LiquidityAwareExecutionScheduler.cpp
  src/MarketExecutionEngine.cpp
)

target_include_directories(QTrading.Execution.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Execution.Library PUBLIC
  QTrading.Risk.Library
  QTrading.Signal.Library
  QTrading.Infra.Library
)

set_target_properties(QTrading.Execution.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
