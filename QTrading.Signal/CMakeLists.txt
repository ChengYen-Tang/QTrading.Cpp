project(QTrading.Signal LANGUAGES CXX)

add_library(QTrading.Signal.Library
  src/SignalModule.cpp
  src/BasisArbitrageSignalEngine.cpp
  src/FundingCarrySignalEngine.cpp
  src/PairMarketSignalSupport.cpp
)

target_include_directories(QTrading.Signal.Library PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QTrading.Signal.Library PUBLIC
  QTrading.Contracts.Library
  QTrading.Infra.Library
)

set_target_properties(QTrading.Signal.Library PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

add_subdirectory(tests)
