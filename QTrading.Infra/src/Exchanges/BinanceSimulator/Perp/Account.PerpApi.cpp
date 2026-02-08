#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

Account::PerpApi::PerpApi(Account& owner)
    : owner_(&owner)
{
}

QTrading::Dto::Account::BalanceSnapshot Account::PerpApi::get_balance() const
{
    return owner_->get_perp_balance();
}

double Account::PerpApi::get_wallet_balance() const
{
    return owner_->get_wallet_balance();
}

double Account::PerpApi::get_margin_balance() const
{
    return owner_->get_margin_balance();
}

double Account::PerpApi::get_available_balance() const
{
    return owner_->get_available_balance();
}

bool Account::PerpApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    return owner_->place_order(symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool Account::PerpApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    return owner_->place_order(symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
}

void Account::PerpApi::close_position(const std::string& symbol, double price)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    owner_->close_position(symbol, price);
}

void Account::PerpApi::close_position(const std::string& symbol,
    PositionSide position_side,
    double price)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    owner_->close_position(symbol, position_side, price);
}

void Account::PerpApi::cancel_open_orders(const std::string& symbol)
{
    owner_->cancel_open_orders(symbol);
}

void Account::PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    owner_->set_symbol_leverage(symbol, new_leverage);
}

double Account::PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    return owner_->get_symbol_leverage(symbol);
}

std::vector<Account::FundingApplyResult> Account::PerpApi::apply_funding(
    const std::string& symbol,
    uint64_t funding_time,
    double rate,
    double mark_price)
{
    owner_->set_instrument_type(symbol, InstrumentType::Perp);
    return owner_->apply_funding(symbol, funding_time, rate, mark_price);
}
