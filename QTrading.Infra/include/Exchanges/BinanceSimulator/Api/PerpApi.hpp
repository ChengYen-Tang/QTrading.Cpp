#pragma once

#include <string>

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

/// Perp order facade for BinanceExchange.
/// Methods are stable contract points even when internals are temporarily stubbed.
class PerpApi {
public:
    explicit PerpApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    /// Perp limit order entrypoint.
    bool place_order(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None,
        QTrading::Dto::Trading::TimeInForce time_in_force = QTrading::Dto::Trading::TimeInForce::GTC);
    bool place_limit_maker(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Perp market order entrypoint.
    bool place_order(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Perp close-position order command entrypoint.
    bool place_close_position_order(const std::string& symbol,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        double price = 0.0, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Convenience close helper for one-way mode style usage.
    void close_position(const std::string& symbol, double price = 0.0);
    /// Convenience close helper with explicit position side.
    void close_position(const std::string& symbol,
        QTrading::Dto::Trading::PositionSide position_side, double price = 0.0);
    /// Cancel all open perp orders for a symbol.
    void cancel_open_orders(const std::string& symbol);
    /// Leverage setters/getters remain forwarded to facade contract.
    void set_symbol_leverage(const std::string& symbol, double new_leverage);
    double get_symbol_leverage(const std::string& symbol) const;

private:
    /// Non-owning facade reference.
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
