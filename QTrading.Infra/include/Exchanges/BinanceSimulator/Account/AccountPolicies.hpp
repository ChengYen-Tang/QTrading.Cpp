#pragma once

#include <functional>
#include <optional>
#include <tuple>

#include "Dto/Market/Binance/TradeKline.hpp"
#include "Dto/Order.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

struct AccountPerSymbolMarketContext {
    const QTrading::Dto::Market::Binance::TradeKlineDto* trade_kline{ nullptr };
    std::optional<double> last_mark_price{};
};

struct AccountPolicies {
    using FeeRates = std::tuple<double, double>;

    std::function<FeeRates(int vip_level)> fee_rates{};
    std::function<std::pair<bool, bool>(const QTrading::dto::Order&, const AccountPerSymbolMarketContext&)> can_fill_and_taker_ctx{};
    std::function<double(const QTrading::dto::Order&, const AccountPerSymbolMarketContext&, double, double)> execution_price_ctx{};

    static AccountPolicies Default();
};

} // namespace QTrading::Infra::Exchanges::BinanceSim

