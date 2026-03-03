#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

Account::SpotApi::SpotApi(Account& owner)
    : owner_(&owner)
{
}

QTrading::Dto::Account::BalanceSnapshot Account::SpotApi::get_balance() const
{
    return owner_->get_spot_balance();
}

double Account::SpotApi::get_cash_balance() const
{
    return owner_->get_spot_cash_balance();
}

bool Account::SpotApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    return owner_->place_order(symbol, quantity, price, side, PositionSide::Both, reduce_only, client_order_id, stp_mode);
}

bool Account::SpotApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    return owner_->place_order(symbol, quantity, side, PositionSide::Both, reduce_only, client_order_id, stp_mode);
}

void Account::SpotApi::close_position(const std::string& symbol, double price)
{
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    owner_->close_position(symbol, PositionSide::Both, price);
}

void Account::SpotApi::cancel_open_orders(const std::string& symbol)
{
    owner_->cancel_open_orders(symbol);
}
