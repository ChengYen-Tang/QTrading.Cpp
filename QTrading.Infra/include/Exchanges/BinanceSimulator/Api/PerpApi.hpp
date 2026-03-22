#pragma once

#include <string>

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

class PerpApi {
public:
    explicit PerpApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    bool place_order(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    bool place_order(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    bool place_close_position_order(const std::string& symbol,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        double price = 0.0, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    void close_position(const std::string& symbol, double price = 0.0);
    void close_position(const std::string& symbol,
        QTrading::Dto::Trading::PositionSide position_side, double price = 0.0);
    void cancel_open_orders(const std::string& symbol);
    void set_symbol_leverage(const std::string& symbol, double new_leverage);
    double get_symbol_leverage(const std::string& symbol) const;

private:
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
