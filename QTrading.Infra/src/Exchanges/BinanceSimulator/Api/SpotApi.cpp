#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

bool SpotApi::place_order(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlaceSpotLimit(
        symbol, quantity, price, side, reduce_only, client_order_id, stp_mode);
}

bool SpotApi::place_order(const std::string& symbol, double quantity,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlaceSpotMarket(
        symbol, quantity, side, reduce_only, client_order_id, stp_mode);
}

bool SpotApi::place_market_order_quote(const std::string& symbol, double quote_order_qty,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlaceSpotMarketQuote(
        symbol, quote_order_qty, side, reduce_only, client_order_id, stp_mode);
}

void SpotApi::close_position(const std::string& symbol, double)
{
    cancel_open_orders(symbol);
}

void SpotApi::cancel_open_orders(const std::string& symbol)
{
    Application::OrderCommandKernel(owner_).CancelOpenOrders(
        QTrading::Dto::Trading::InstrumentType::Spot,
        symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
