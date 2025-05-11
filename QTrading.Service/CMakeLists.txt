project(QTrading.Service LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED COMPONENTS thread)

add_executable(QTrading.Service
  QTrading.Service.cpp
  QTrading.Service.h
)

target_include_directories(QTrading.Service PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.DataPreprocess/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.Infra/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../QTrading.Strategy/include
)

target_link_libraries(QTrading.Service
  PRIVATE
    QTrading.Infra.Library
    QTrading.DataPreprocess.Library
    QTrading.Strategy.Library
    Boost::thread
)

set_target_properties(QTrading.Service PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
