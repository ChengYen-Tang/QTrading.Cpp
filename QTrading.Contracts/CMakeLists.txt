project(QTrading.Contracts LANGUAGES CXX)

add_library(QTrading.Contracts.Library INTERFACE)

target_include_directories(QTrading.Contracts.Library INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
