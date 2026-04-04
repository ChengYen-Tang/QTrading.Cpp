#include "Intent/BasisArbitrageIntentBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace QTrading::Intent {

BasisArbitrageIntentBuilder::BasisArbitrageIntentBuilder(Config cfg)
    : FundingCarryIntentBuilder(cfg)
    , cfg_(std::move(cfg))
    , current_receive_funding_(cfg_.receive_funding)
{
    if (!std::isfinite(cfg_.basis_direction_switch_entry_abs_pct) ||
        cfg_.basis_direction_switch_entry_abs_pct < 0.0)
    {
        cfg_.basis_direction_switch_entry_abs_pct = 0.005;
    }
    if (!std::isfinite(cfg_.basis_direction_switch_exit_abs_pct) ||
        cfg_.basis_direction_switch_exit_abs_pct < 0.0)
    {
        cfg_.basis_direction_switch_exit_abs_pct = 0.0015;
    }
    cfg_.basis_direction_switch_exit_abs_pct =
        std::min(cfg_.basis_direction_switch_exit_abs_pct, cfg_.basis_direction_switch_entry_abs_pct);
}

TradeIntent BasisArbitrageIntentBuilder::build(
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    TradeIntent out = FundingCarryIntentBuilder::build(signal, market);
    out.strategy = "basis_arbitrage";
    out.structure = "delta_neutral_basis";
    out.reason = "basis_arbitrage";
    if (!out.intent_id.empty()) {
        constexpr std::string_view old_prefix = "funding_carry:";
        if (out.intent_id.rfind(old_prefix, 0) == 0) {
            out.intent_id.replace(0, old_prefix.size(), "basis_arbitrage:");
        }
    }

    if (signal.status != QTrading::Signal::SignalStatus::Active || out.legs.size() != 2) {
        return out;
    }

    bool use_receive_funding_side = current_receive_funding_;
    if (cfg_.basis_directional_enabled) {
        const auto basis_pct_opt = ComputeBasisPct(market);
        if (basis_pct_opt.has_value() && std::isfinite(*basis_pct_opt)) {
            const double basis_pct = *basis_pct_opt;
            const double abs_basis = std::fabs(basis_pct);
            const bool can_switch =
                (cfg_.basis_direction_switch_cooldown_ms == 0) ||
                (last_direction_switch_ts_ == 0) ||
                (signal.ts_ms >= last_direction_switch_ts_ + cfg_.basis_direction_switch_cooldown_ms);

            if (abs_basis >= cfg_.basis_direction_switch_entry_abs_pct && can_switch) {
                const bool desired_receive = basis_pct >= 0.0;
                if (desired_receive != current_receive_funding_) {
                    current_receive_funding_ = desired_receive;
                    last_direction_switch_ts_ = signal.ts_ms;
                }
            }
            else if (abs_basis <= cfg_.basis_direction_switch_exit_abs_pct) {
                // Inside dead-band: keep current direction and suppress sign-noise flipping.
            }
        }
        use_receive_funding_side = current_receive_funding_;
    }

    ApplyLegDirection(out, use_receive_funding_side);
    if (!out.intent_id.empty()) {
        const std::string suffix = use_receive_funding_side ? ":receive" : ":pay";
        const auto pos = out.intent_id.rfind(':');
        if (pos != std::string::npos) {
            out.intent_id = out.intent_id.substr(0, pos) + suffix;
        }
    }
    return out;
}

bool BasisArbitrageIntentBuilder::ResolveSymbolIds(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    if (has_symbol_ids_) {
        return true;
    }
    if (!market || !market->symbols) {
        return false;
    }
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] == cfg_.spot_symbol) {
            spot_id_ = i;
        }
        if (symbols[i] == cfg_.perp_symbol) {
            perp_id_ = i;
        }
    }
    const bool spot_ok = spot_id_ < symbols.size() && symbols[spot_id_] == cfg_.spot_symbol;
    const bool perp_ok = perp_id_ < symbols.size() && symbols[perp_id_] == cfg_.perp_symbol;
    has_symbol_ids_ = spot_ok && perp_ok;
    return has_symbol_ids_;
}

std::optional<double> BasisArbitrageIntentBuilder::ComputeBasisPct(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) const
{
    // const helper keeps call-sites simple; mutable state (id resolve) done in build().
    if (!market) {
        return std::nullopt;
    }

    // ResolveSymbolIds mutates state; call through const_cast to preserve public signature simplicity.
    auto* self = const_cast<BasisArbitrageIntentBuilder*>(this);
    if (!self->ResolveSymbolIds(market)) {
        return std::nullopt;
    }

    if (cfg_.basis_direction_use_mark_index &&
        perp_id_ < market->mark_klines_by_id.size() &&
        perp_id_ < market->index_klines_by_id.size())
    {
        const auto& mark_opt = market->mark_klines_by_id[perp_id_];
        const auto& index_opt = market->index_klines_by_id[perp_id_];
        if (mark_opt.has_value() && index_opt.has_value() && index_opt->ClosePrice > 0.0) {
            return (mark_opt->ClosePrice - index_opt->ClosePrice) / index_opt->ClosePrice;
        }
    }

    if (spot_id_ >= market->trade_klines_by_id.size() || perp_id_ >= market->trade_klines_by_id.size()) {
        return std::nullopt;
    }
    const auto& spot_opt = market->trade_klines_by_id[spot_id_];
    const auto& perp_opt = market->trade_klines_by_id[perp_id_];
    if (!spot_opt.has_value() || !perp_opt.has_value() || spot_opt->ClosePrice <= 0.0) {
        return std::nullopt;
    }
    return (perp_opt->ClosePrice - spot_opt->ClosePrice) / spot_opt->ClosePrice;
}

void BasisArbitrageIntentBuilder::ApplyLegDirection(TradeIntent& intent, bool receive_funding) const
{
    intent.legs.clear();
    if (receive_funding) {
        intent.legs.push_back(TradeLeg{ cfg_.spot_symbol, TradeSide::Long });
        intent.legs.push_back(TradeLeg{ cfg_.perp_symbol, TradeSide::Short });
    }
    else {
        intent.legs.push_back(TradeLeg{ cfg_.spot_symbol, TradeSide::Short });
        intent.legs.push_back(TradeLeg{ cfg_.perp_symbol, TradeSide::Long });
    }
}

} // namespace QTrading::Intent
