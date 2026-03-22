#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

bool SpotApi::place_order(const std::string&, double, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    // Explicitly throws to keep missing feature visible while API stays stable.
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_order(limit)");
}

bool SpotApi::place_order(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_order(market)");
}

bool SpotApi::place_market_order_quote(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, bool, const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::place_market_order_quote");
}

void SpotApi::close_position(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::close_position");
}

void SpotApi::cancel_open_orders(const std::string&)
{
    Support::ThrowNotImplemented("BinanceExchange::SpotApi::cancel_open_orders");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
