#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

bool OrderEntryService::PlaceSpotLimitSync(
    Account& account,
    const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    return account.spot.place_order(symbol, quantity, price, side, reduce_only, client_order_id, stp_mode);
}

bool OrderEntryService::PlaceSpotMarketSync(
    Account& account,
    const std::string& symbol,
    double quantity,
    QTrading::Dto::Trading::OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    return account.spot.place_order(symbol, quantity, side, reduce_only, client_order_id, stp_mode);
}

bool OrderEntryService::PlaceSpotMarketQuoteSync(
    Account& account,
    const std::string& symbol,
    double quote_order_qty,
    QTrading::Dto::Trading::OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    return account.spot.place_market_order_quote(
        symbol, quote_order_qty, side, reduce_only, client_order_id, stp_mode);
}

bool OrderEntryService::PlacePerpLimitSync(
    Account& account,
    const PerpOpeningBlockEvaluator& opening_blocked,
    const PerpOpeningBlockRecorder& on_blocked,
    const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    if (opening_blocked && opening_blocked(symbol, side, position_side, reduce_only)) {
        if (on_blocked) {
            on_blocked();
        }
        return false;
    }

    return account.perp.place_order(
        symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool OrderEntryService::PlacePerpMarketSync(
    Account& account,
    const PerpOpeningBlockEvaluator& opening_blocked,
    const PerpOpeningBlockRecorder& on_blocked,
    const std::string& symbol,
    double quantity,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    if (opening_blocked && opening_blocked(symbol, side, position_side, reduce_only)) {
        if (on_blocked) {
            on_blocked();
        }
        return false;
    }

    return account.perp.place_order(
        symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
