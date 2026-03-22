#pragma once

#include <functional>
#include <string>

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class OrderEntryService final {
public:
    using PerpOpeningBlockEvaluator = std::function<bool(
        const std::string& symbol,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only)>;

    using PerpOpeningBlockRecorder = std::function<void()>;

    static bool PlaceSpotLimitSync(
        Account& account,
        const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        bool reduce_only,
        const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode);

    static bool PlaceSpotMarketSync(
        Account& account,
        const std::string& symbol,
        double quantity,
        QTrading::Dto::Trading::OrderSide side,
        bool reduce_only,
        const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode);

    static bool PlaceSpotMarketQuoteSync(
        Account& account,
        const std::string& symbol,
        double quote_order_qty,
        QTrading::Dto::Trading::OrderSide side,
        bool reduce_only,
        const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode);

    static bool PlacePerpLimitSync(
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
        Account::SelfTradePreventionMode stp_mode);

    static bool PlacePerpMarketSync(
        Account& account,
        const PerpOpeningBlockEvaluator& opening_blocked,
        const PerpOpeningBlockRecorder& on_blocked,
        const std::string& symbol,
        double quantity,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only,
        const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode);

};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
