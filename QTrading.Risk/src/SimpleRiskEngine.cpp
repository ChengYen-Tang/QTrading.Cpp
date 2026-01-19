#include "Risk/SimpleRiskEngine.hpp"

namespace QTrading::Risk {

SimpleRiskEngine::SimpleRiskEngine(Config cfg)
    : cfg_(cfg)
{
}

RiskTarget SimpleRiskEngine::position(const QTrading::Intent::TradeIntent& intent,
    const AccountState& account,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    (void)account;
    (void)market;

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
