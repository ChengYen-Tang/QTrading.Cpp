add_library(QTrading.Strategy.Library STATIC
  Trend/UTBotStrategy.cpp
)

target_include_directories(QTrading.Strategy.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.DataPreprocess/include
)

if(QTRADING_ENABLE_TRACE)
  target_compile_definitions(QTrading.Strategy.Library PRIVATE QTRADING_TRACE)
  if(QTRADING_TRACE_VERBOSE)
    target_compile_definitions(QTrading.Strategy.Library PRIVATE QTRADING_TRACE_VERBOSE)
  endif()
endif()

target_link_libraries(QTrading.Strategy.Library
  PRIVATE
    Boost::thread
)
