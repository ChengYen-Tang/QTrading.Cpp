#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

bool PerpApi::place_order(const std::string&, double, double,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, bool,
    const std::string&, Account::SelfTradePreventionMode)
{
    // Explicit stub while preserving outward facade behavior.
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_order(limit)");
}

bool PerpApi::place_order(const std::string&, double,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, bool,
    const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_order(market)");
}

bool PerpApi::place_close_position_order(const std::string&,
    QTrading::Dto::Trading::OrderSide, QTrading::Dto::Trading::PositionSide, double,
    const std::string&, Account::SelfTradePreventionMode)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::place_close_position_order");
}

void PerpApi::close_position(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::close_position(symbol,price)");
}

void PerpApi::close_position(const std::string&,
    QTrading::Dto::Trading::PositionSide, double)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::close_position(symbol,position_side,price)");
}

void PerpApi::cancel_open_orders(const std::string&)
{
    Support::ThrowNotImplemented("BinanceExchange::PerpApi::cancel_open_orders");
}

void PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    owner_.set_symbol_leverage(symbol, new_leverage);
}

double PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    return owner_.get_symbol_leverage(symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api

namespace QTrading::Infra::Exchanges::BinanceSim {

void BinanceExchange::set_symbol_leverage(const std::string&, double)
{
    Support::ThrowNotImplemented("BinanceExchange::set_symbol_leverage");
}

double BinanceExchange::get_symbol_leverage(const std::string&) const
{
    Support::ThrowNotImplemented("BinanceExchange::get_symbol_leverage");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
