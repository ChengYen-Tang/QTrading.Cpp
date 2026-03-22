#pragma once

#include <string>

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

/// Spot order facade for BinanceExchange.
/// In current skeleton this preserves method contracts while execution is rebuilt.
class SpotApi {
public:
    explicit SpotApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    /// Spot limit order entrypoint (contract preserved; implementation pending).
    bool place_order(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only = false,
        const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Spot market order by base quantity (contract preserved; implementation pending).
    bool place_order(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only = false,
        const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Spot market buy/sell by quote amount (contract preserved; implementation pending).
    bool place_market_order_quote(const std::string& symbol, double quote_order_qty,
        QTrading::Dto::Trading::OrderSide side = QTrading::Dto::Trading::OrderSide::Buy,
        bool reduce_only = false, const std::string& client_order_id = {},
        Account::SelfTradePreventionMode stp_mode = Account::SelfTradePreventionMode::None);
    /// Spot position close helper (contract preserved; implementation pending).
    void close_position(const std::string& symbol, double price = 0.0);
    /// Spot cancel-open-orders helper (contract preserved; implementation pending).
    void cancel_open_orders(const std::string& symbol);

private:
    /// Non-owning facade reference.
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
