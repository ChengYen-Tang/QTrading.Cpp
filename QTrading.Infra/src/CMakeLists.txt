﻿SET(SOURCE_FILES
  "Exanges/BinanceSimulator/Futures/Account.cpp"
  "Exanges/BinanceSimulator/DataProvider/MarketData.cpp"
)

add_library (QTrading.Infra.Library STATIC ${SOURCE_FILES})
target_include_directories(QTrading.Infra.Library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include", "${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include")

target_link_libraries(QTrading.Infra.Library ${Boost_LIBRARIES} Boost::filesystem Boost::system)
