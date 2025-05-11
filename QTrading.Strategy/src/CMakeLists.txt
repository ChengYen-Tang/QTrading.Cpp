add_library(QTrading.Strategy.Library STATIC
  Trend/UTBotStrategy.cpp
)

target_include_directories(QTrading.Strategy.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.DataPreprocess/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Infra/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
)

target_link_libraries(QTrading.Strategy.Library
  PRIVATE
    Boost::thread
)
