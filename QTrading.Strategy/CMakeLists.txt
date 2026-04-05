project(QTrading.Strategy LANGUAGES CXX)

add_library(QTrading.Strategy.Library
  src/FundingCarryStrategyGateway.cpp
  src/FundingCarryStrategyRuntime.cpp
  src/StrategyConfigLoader.cpp
  src/StrategyModuleBuilder.cpp
)

target_include_directories(QTrading.Strategy.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Strategy.Library PUBLIC
  QTrading.Contracts.Library
  QTrading.Universe.Library
  QTrading.Signal.Library
  QTrading.Intent.Library
  QTrading.Risk.Library
  QTrading.Execution.Library
  QTrading.Monitoring.Library
  QTrading.Infra.Library
)

set_target_properties(QTrading.Strategy.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
