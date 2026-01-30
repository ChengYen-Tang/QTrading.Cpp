#include "Risk/SimpleRiskEngine.hpp"

#include <cmath>
#include <optional>
#include <unordered_map>

namespace {

std::optional<double> GetPriceBySymbol(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& symbol)
{
    if (!market) {
        return std::nullopt;
    }

    if (market->symbols && !market->klines_by_id.empty()) {
        const auto& symbols = *market->symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == symbol && i < market->klines_by_id.size()) {
                const auto& opt = market->klines_by_id[i];
                if (opt.has_value()) {
                    return opt->ClosePrice;
                }
                return std::nullopt;
            }
        }
    }

    const auto& klines = market->klines;
    auto it = klines.find(symbol);
    if (it == klines.end() || !it->second.has_value()) {
        return std::nullopt;
    }
    return it->second->ClosePrice;
}

double SignedNotionalFromPosition(const QTrading::dto::Position& pos, double price)
{
    const double sign = pos.is_long ? 1.0 : -1.0;
    return pos.quantity * price * sign;
}

} // namespace

namespace QTrading::Risk {

SimpleRiskEngine::SimpleRiskEngine(Config cfg)
    : cfg_(cfg)
{
}

RiskTarget SimpleRiskEngine::position(const QTrading::Intent::TradeIntent& intent,
    const AccountState& account,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    RiskTarget out;
    out.ts_ms = intent.ts_ms;
    out.strategy = intent.strategy;
    out.max_leverage = cfg_.max_leverage;
    out.risk_budget_used = intent.legs.empty() ? 0.0 : 1.0;

    if (intent.legs.empty()) {
        for (const auto& pos : account.positions) {
            out.target_positions[pos.symbol] = 0.0;
            out.leverage[pos.symbol] = cfg_.leverage;
        }
        return out;
    }

    const bool is_carry = (intent.strategy == "funding_carry") ||
        (intent.structure == "delta_neutral_carry");

    if (is_carry && market && intent.legs.size() >= 2) {
        double gross = 0.0;
        double net = 0.0;
        std::unordered_map<std::string, double> current_notional;
        current_notional.reserve(intent.legs.size());

        for (const auto& leg : intent.legs) {
            auto price = GetPriceBySymbol(market, leg.instrument);
            if (!price.has_value() || *price <= 0.0) {
                current_notional.clear();
                break;
            }

            double notional = 0.0;
            for (const auto& pos : account.positions) {
                if (pos.symbol == leg.instrument) {
                    notional += SignedNotionalFromPosition(pos, *price);
                }
            }

            current_notional[leg.instrument] = notional;
            gross += std::abs(notional);
            net += notional;
        }

        if (!current_notional.empty() && gross > 0.0) {
            const double ratio = std::abs(net) / gross;
            if (ratio < cfg_.rebalance_threshold_ratio) {
                for (const auto& leg : intent.legs) {
                    out.target_positions[leg.instrument] = current_notional[leg.instrument];
                    out.leverage[leg.instrument] = cfg_.leverage;
                }
                return out;
            }
        }
    }

    for (const auto& leg : intent.legs) {
        const double signed_notional = (leg.side == QTrading::Intent::TradeSide::Long)
            ? cfg_.notional_usdt
            : -cfg_.notional_usdt;
        out.target_positions[leg.instrument] = signed_notional;
        out.leverage[leg.instrument] = cfg_.leverage;
    }

    return out;
}

} // namespace QTrading::Risk
