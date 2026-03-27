#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Dto/Market/Binance/TradeKline.hpp"
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct AccountPolicyUpdateResult {
    double perp_wallet_delta{ 0.0 };
    uint32_t filled_count{ 0 };
};

class AccountPolicyExecutionService final {
public:
    static bool TryQueueOrder(
        const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only,
        int& next_order_id,
        std::vector<QTrading::dto::Order>& open_orders);

    static AccountPolicyUpdateResult ApplyUpdates(
        const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline,
        const std::unordered_map<std::string, double>& symbol_mark_price,
        const AccountPolicies& policies,
        int vip_level,
        const std::unordered_map<std::string, double>& symbol_leverage,
        std::vector<QTrading::dto::Order>& open_orders,
        std::vector<QTrading::dto::Position>& positions);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain

