#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

bool Api::SpotApi::place_order(const std::string&, double, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_order(limit)");
}

bool Api::SpotApi::place_order(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_order(market)");
}

bool Api::SpotApi::place_market_order_quote(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_market_order_quote");
}

void Api::SpotApi::close_position(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::close_position");
}

void Api::SpotApi::cancel_open_orders(const std::string&)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::cancel_open_orders");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
