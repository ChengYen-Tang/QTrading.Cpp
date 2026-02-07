#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace QTrading::Infra::Exchanges::BinanceSim {

bool BinanceExchange::PerpApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->perp.place_order(symbol, quantity, price, side, position_side, reduce_only);
}

bool BinanceExchange::PerpApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->perp.place_order(symbol, quantity, side, position_side, reduce_only);
}

void BinanceExchange::PerpApi::close_position(const std::string& symbol, double price)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.close_position(symbol, price);
}

void BinanceExchange::PerpApi::close_position(const std::string& symbol,
    PositionSide position_side,
    double price)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.close_position(symbol, position_side, price);
}

void BinanceExchange::PerpApi::cancel_open_orders(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.cancel_open_orders(symbol);
}

void BinanceExchange::PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.set_symbol_leverage(symbol, new_leverage);
}

double BinanceExchange::PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->perp.get_symbol_leverage(symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
