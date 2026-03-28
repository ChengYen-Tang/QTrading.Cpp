#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr double kSpotMakerFeeRate = 0.001;
constexpr double kSpotTakerFeeRate = 0.001;
constexpr double kPerpMakerFeeRate = 0.0002;
constexpr double kPerpTakerFeeRate = 0.0005;
constexpr double kEpsilon = 1e-12;

bool spot_buy_fee_paid_in_base(const State::BinanceExchangeRuntimeState& runtime_state)
{
    return runtime_state.simulation_config.spot_commission_mode ==
        Config::SpotCommissionMode::BaseOnBuyQuoteOnSell;
}

double fee_rate_for_fill(const MatchFill& fill)
{
    if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
        return fill.is_taker ? kSpotTakerFeeRate : kSpotMakerFeeRate;
    }
    return fill.is_taker ? kPerpTakerFeeRate : kPerpMakerFeeRate;
}

int next_position_id(State::BinanceExchangeRuntimeState& runtime_state)
{
    int max_id = 0;
    for (const auto& position : runtime_state.positions) {
        if (position.id > max_id) {
            max_id = position.id;
        }
    }
    return max_id + 1;
}

QTrading::dto::Position* find_spot_inventory_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    for (auto& position : runtime_state.positions) {
        if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot &&
            position.symbol == symbol &&
            position.is_long) {
            return &position;
        }
    }
    return nullptr;
}

QTrading::dto::Position* find_perp_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    for (auto& position : runtime_state.positions) {
        if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
            position.symbol == symbol) {
            return &position;
        }
    }
    return nullptr;
}

void apply_spot_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const MatchFill& fill)
{
    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(fill);
    const double fee = notional * fee_rate;
    const bool base_fee_on_buy = spot_buy_fee_paid_in_base(runtime_state) &&
        fill.side == QTrading::Dto::Trading::OrderSide::Buy;
    const double quantity_fee = base_fee_on_buy ? fill.quantity * fee_rate : 0.0;
    const double net_quantity = fill.quantity - quantity_fee;

    if (fill.side == QTrading::Dto::Trading::OrderSide::Buy) {
        account.apply_spot_cash_delta(-(notional + (base_fee_on_buy ? 0.0 : fee)));

        auto* position = find_spot_inventory_position(runtime_state, fill.symbol);
        if (!position) {
            QTrading::dto::Position created{};
            created.id = next_position_id(runtime_state);
            created.order_id = fill.order_id;
            created.symbol = fill.symbol;
            created.quantity = net_quantity;
            created.entry_price = fill.price;
            created.is_long = true;
            created.notional = created.quantity * created.entry_price;
            created.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
            runtime_state.positions.emplace_back(std::move(created));
            return;
        }

        const double before = position->quantity;
        const double after = before + net_quantity;
        if (after > kEpsilon) {
            position->entry_price = ((position->entry_price * before) + (fill.price * net_quantity)) / after;
        }
        position->quantity = after;
        position->notional = after * position->entry_price;
        position->fee += fee;
        position->fee_rate = fee_rate;
        return;
    }

    account.apply_spot_cash_delta(notional - fee);
    auto* position = find_spot_inventory_position(runtime_state, fill.symbol);
    if (!position) {
        return;
    }
    position->quantity = std::max(0.0, position->quantity - fill.quantity);
    position->notional = position->quantity * position->entry_price;
    if (position->quantity <= kEpsilon) {
        runtime_state.positions.erase(
            std::remove_if(runtime_state.positions.begin(), runtime_state.positions.end(),
                [&](const QTrading::dto::Position& candidate) {
                    return candidate.id == position->id;
                }),
            runtime_state.positions.end());
    }
}

void apply_perp_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const MatchFill& fill)
{
    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(fill);
    const double fee = notional * fee_rate;
    double signed_fill = fill.side == QTrading::Dto::Trading::OrderSide::Buy ? fill.quantity : -fill.quantity;
    auto* position = find_perp_position(runtime_state, fill.symbol);
    if (fill.reduce_only) {
        if (!position) {
            return;
        }
        const double current_signed = position->is_long ? position->quantity : -position->quantity;
        if (current_signed * signed_fill >= 0.0) {
            return;
        }
        const double reducible = std::abs(current_signed);
        const double requested = std::abs(signed_fill);
        const double effective = std::min(reducible, requested);
        signed_fill = signed_fill > 0.0 ? effective : -effective;
        if (effective <= kEpsilon) {
            return;
        }
    }
    account.apply_perp_wallet_delta(-fee);

    if (!position) {
        QTrading::dto::Position created{};
        created.id = next_position_id(runtime_state);
        created.order_id = fill.order_id;
        created.symbol = fill.symbol;
        created.quantity = std::abs(signed_fill);
        created.entry_price = fill.price;
        created.is_long = signed_fill > 0.0;
        created.notional = created.quantity * created.entry_price;
        created.initial_margin = 0.0;
        created.maintenance_margin = 0.0;
        created.fee = fee;
        created.leverage = 1.0;
        created.fee_rate = fee_rate;
        created.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
        runtime_state.positions.emplace_back(std::move(created));
        return;
    }

    const double current_signed = position->is_long ? position->quantity : -position->quantity;
    const double next_signed = current_signed + signed_fill;
    if (std::abs(next_signed) <= kEpsilon) {
        const int closed_id = position->id;
        runtime_state.positions.erase(
            std::remove_if(runtime_state.positions.begin(), runtime_state.positions.end(),
                [&](const QTrading::dto::Position& candidate) {
                    return candidate.id == closed_id;
                }),
            runtime_state.positions.end());
        return;
    }

    if ((current_signed > 0.0 && signed_fill > 0.0) || (current_signed < 0.0 && signed_fill < 0.0)) {
        const double before = std::abs(current_signed);
        const double after = std::abs(next_signed);
        position->entry_price = ((position->entry_price * before) + (fill.price * std::abs(signed_fill))) / after;
    }
    else if ((current_signed > 0.0 && next_signed < 0.0) || (current_signed < 0.0 && next_signed > 0.0)) {
        position->entry_price = fill.price;
    }

    position->quantity = std::abs(next_signed);
    position->is_long = next_signed > 0.0;
    position->notional = position->quantity * position->entry_price;
    position->fee += fee;
    position->fee_rate = fee_rate;
    position->instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
}

} // namespace

void FillSettlementEngine::Apply(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const std::vector<MatchFill>& fills)
{
    for (const auto& fill : fills) {
        if (fill.quantity <= kEpsilon || fill.price <= 0.0) {
            continue;
        }
        if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
            apply_spot_fill(runtime_state, account, fill);
            continue;
        }
        apply_perp_fill(runtime_state, account, fill);
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
