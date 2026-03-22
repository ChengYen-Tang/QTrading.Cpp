#pragma once

#include <string>

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

class SpotApi {
public:
    explicit SpotApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    bool place_order(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only = false,
        const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    bool place_order(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only = false,
        const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    bool place_market_order_quote(const std::string& symbol, double quote_order_qty,
        QTrading::Dto::Trading::OrderSide side = QTrading::Dto::Trading::OrderSide::Buy,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    void close_position(const std::string& symbol, double price = 0.0);
    void cancel_open_orders(const std::string& symbol);

private:
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
