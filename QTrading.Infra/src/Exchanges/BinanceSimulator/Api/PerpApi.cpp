#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

bool PerpApi::place_order(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpLimit(
        symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool PerpApi::place_order(const std::string& symbol, double quantity,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpMarket(
        symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool PerpApi::place_close_position_order(const std::string& symbol,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, double price,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpClosePosition(
        symbol, side, position_side, price, client_order_id, stp_mode);
}

void PerpApi::close_position(const std::string& symbol, double price)
{
    (void)place_close_position_order(
        symbol,
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Both,
        price);
}

void PerpApi::close_position(const std::string& symbol,
    QTrading::Dto::Trading::PositionSide position_side, double price)
{
    const auto side = position_side == QTrading::Dto::Trading::PositionSide::Short
        ? QTrading::Dto::Trading::OrderSide::Buy
        : QTrading::Dto::Trading::OrderSide::Sell;
    (void)place_close_position_order(symbol, side, position_side, price);
}

void PerpApi::cancel_open_orders(const std::string& symbol)
{
    Application::OrderCommandKernel(owner_).CancelOpenOrders(
        QTrading::Dto::Trading::InstrumentType::Perp,
        symbol);
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
    // Leverage is not modeled in the current hard-prune skeleton.
}

double BinanceExchange::get_symbol_leverage(const std::string&) const
{
    return 1.0;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
