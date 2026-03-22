#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

bool Api::PerpApi::place_order(const std::string&, double, double,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, bool,
    const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_order(limit)");
}

bool Api::PerpApi::place_order(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, bool,
    const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_order(market)");
}

bool Api::PerpApi::place_close_position_order(const std::string&,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, double,
    const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_close_position_order");
}

void Api::PerpApi::close_position(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::close_position(symbol,price)");
}

void Api::PerpApi::close_position(const std::string&,
    QTrading::Dto::Trading::PositionSide, double)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::close_position(symbol,position_side,price)");
}

void Api::PerpApi::cancel_open_orders(const std::string&)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::cancel_open_orders");
}

void Api::PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    owner_.set_symbol_leverage(symbol, new_leverage);
}

double Api::PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    return owner_.get_symbol_leverage(symbol);
}

void BinanceExchange::set_symbol_leverage(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::set_symbol_leverage");
}

double BinanceExchange::get_symbol_leverage(const std::string&) const
{
    Support::ThrowNotImplemented("BinanceExchange::get_symbol_leverage");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
