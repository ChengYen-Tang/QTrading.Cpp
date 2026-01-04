add_library(QTrading.Infra.Library STATIC
  Exchanges/BinanceSimulator/Futures/Account.cpp
  Exchanges/BinanceSimulator/Futures/Account.Opening.cpp
  Exchanges/BinanceSimulator/Futures/AccountPolicies.cpp
  Exchanges/BinanceSimulator/DataProvider/MarketData.cpp
  Exchanges/BinanceSimulator/BinanceExchange.cpp
)

target_include_directories(QTrading.Infra.Library
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Logging/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../QTrading.Utils/include
)

target_link_libraries(QTrading.Infra.Library
  PRIVATE
    QTrading.Logging.Library
    Boost::filesystem
    Boost::system
)
