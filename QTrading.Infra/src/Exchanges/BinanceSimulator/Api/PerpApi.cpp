#include <cmath>
#include <limits>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

bool PerpApi::place_order(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode,
    QTrading::Dto::Trading::TimeInForce time_in_force)
{
    return Application::OrderCommandKernel(owner_).PlacePerpLimit(
        symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode, time_in_force);
}

bool PerpApi::place_limit_maker(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return place_order(
        symbol,
        quantity,
        price,
        side,
        position_side,
        reduce_only,
        client_order_id,
        stp_mode,
        QTrading::Dto::Trading::TimeInForce::GTX);
}

bool PerpApi::place_order(const std::string& symbol, double quantity,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, bool reduce_only,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpMarket(
        symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool PerpApi::place_close_position_order(const std::string& symbol,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side, double price,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpClosePosition(
        symbol, side, position_side, price, client_order_id, stp_mode);
}

void PerpApi::close_position(const std::string& symbol, double price)
{
    if (place_close_position_order(
            symbol,
            QTrading::Dto::Trading::OrderSide::Sell,
            QTrading::Dto::Trading::PositionSide::Both,
            price)) {
        return;
    }

    // Hedge-mode fallback for convenience close-by-symbol API:
    // attempt closing each side explicitly when BOTH is rejected.
    (void)place_close_position_order(
        symbol,
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Long,
        price);
    (void)place_close_position_order(
        symbol,
        QTrading::Dto::Trading::OrderSide::Buy,
        QTrading::Dto::Trading::PositionSide::Short,
        price);
}

void PerpApi::close_position(const std::string& symbol,
    QTrading::Dto::Trading::PositionSide position_side, double price)
{
    const auto side = position_side == QTrading::Dto::Trading::PositionSide::Short
        ? QTrading::Dto::Trading::OrderSide::Buy
        : QTrading::Dto::Trading::OrderSide::Sell;
    (void)place_close_position_order(symbol, side, position_side, price);
}

void PerpApi::cancel_open_orders(const std::string& symbol)
{
    Application::OrderCommandKernel(owner_).CancelOpenOrders(
        QTrading::Dto::Trading::InstrumentType::Perp,
        symbol);
}

void PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    owner_.set_symbol_leverage(symbol, new_leverage);
}

double PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    return owner_.get_symbol_leverage(symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api

namespace QTrading::Infra::Exchanges::BinanceSim {

namespace {

bool is_spot_symbol(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const auto it = step_state.symbol_to_id.find(symbol);
    if (it == step_state.symbol_to_id.end()) {
        return false;
    }
    const size_t symbol_id = it->second;
    if (symbol_id >= step_state.symbol_instrument_type_by_id.size()) {
        return false;
    }
    return step_state.symbol_instrument_type_by_id[symbol_id] ==
        QTrading::Dto::Trading::InstrumentType::Spot;
}

const std::vector<MarginTier>& resolve_symbol_margin_tiers(
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept
{
    if (symbol_id < step_state.symbol_maintenance_margin_tiers_by_id.size()) {
        const auto& symbol_tiers = step_state.symbol_maintenance_margin_tiers_by_id[symbol_id];
        if (!symbol_tiers.empty()) {
            return symbol_tiers;
        }
    }
    return ::margin_tiers;
}

double symbol_max_leverage(
    const State::StepKernelState& step_state,
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    double max_allowed = std::numeric_limits<double>::max();
    size_t symbol_id = std::numeric_limits<size_t>::max();

    const auto it = step_state.symbol_to_id.find(symbol);
    if (it != step_state.symbol_to_id.end()) {
        symbol_id = it->second;
        if (symbol_id < step_state.symbol_spec_by_id.size()) {
            const double spec_max = step_state.symbol_spec_by_id[symbol_id].max_leverage;
            if (spec_max > 0.0 && std::isfinite(spec_max)) {
                max_allowed = std::min(max_allowed, spec_max);
            }
        }
    }

    double symbol_notional = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.symbol != symbol ||
            position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        symbol_notional += std::abs(position.notional);
    }

    const auto& tiers = symbol_id != std::numeric_limits<size_t>::max()
        ? resolve_symbol_margin_tiers(step_state, symbol_id)
        : ::margin_tiers;
    for (const auto& tier : tiers) {
        if (symbol_notional <= tier.notional_upper) {
            max_allowed = std::min(max_allowed, tier.max_leverage);
            break;
        }
    }

    if (!(max_allowed > 0.0) || !std::isfinite(max_allowed)) {
        return 1.0;
    }
    return max_allowed;
}

} // namespace

void BinanceExchange::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    if (!(new_leverage > 0.0) || !std::isfinite(new_leverage)) {
        return;
    }
    if (is_spot_symbol(*step_kernel_state_, symbol)) {
        return;
    }
    const double max_allowed = symbol_max_leverage(*step_kernel_state_, *runtime_state_, symbol);
    if (new_leverage > max_allowed) {
        return;
    }
    runtime_state_->symbol_leverage[symbol] = new_leverage;
}

double BinanceExchange::get_symbol_leverage(const std::string& symbol) const
{
    if (is_spot_symbol(*step_kernel_state_, symbol)) {
        return 1.0;
    }
    const auto it = runtime_state_->symbol_leverage.find(symbol);
    if (it == runtime_state_->symbol_leverage.end()) {
        return 1.0;
    }
    return it->second;
}

void BinanceExchange::set_spot_symbol_fee_rate(
    const std::string& symbol,
    double maker_fee_rate,
    double taker_fee_rate)
{
    if (!(maker_fee_rate >= 0.0) ||
        !(taker_fee_rate >= 0.0) ||
        !std::isfinite(maker_fee_rate) ||
        !std::isfinite(taker_fee_rate)) {
        return;
    }
    runtime_state_->spot_symbol_fee_overrides[symbol] = State::SymbolFeeRateOverride{
        maker_fee_rate,
        taker_fee_rate };
}

void BinanceExchange::set_perp_symbol_fee_rate(
    const std::string& symbol,
    double maker_fee_rate,
    double taker_fee_rate)
{
    if (!(maker_fee_rate >= 0.0) ||
        !(taker_fee_rate >= 0.0) ||
        !std::isfinite(maker_fee_rate) ||
        !std::isfinite(taker_fee_rate)) {
        return;
    }
    runtime_state_->perp_symbol_fee_overrides[symbol] = State::SymbolFeeRateOverride{
        maker_fee_rate,
        taker_fee_rate };
}

void BinanceExchange::set_perp_symbol_liquidation_fee_rate(
    const std::string& symbol,
    double liquidation_fee_rate)
{
    if (!(liquidation_fee_rate >= 0.0) ||
        !std::isfinite(liquidation_fee_rate)) {
        return;
    }
    const auto symbol_it = step_kernel_state_->symbol_to_id.find(symbol);
    if (symbol_it == step_kernel_state_->symbol_to_id.end()) {
        return;
    }
    const size_t symbol_id = symbol_it->second;
    if (symbol_id >= step_kernel_state_->symbol_spec_by_id.size()) {
        return;
    }
    auto& spec = step_kernel_state_->symbol_spec_by_id[symbol_id];
    if (spec.type != QTrading::Dto::Trading::InstrumentType::Perp) {
        return;
    }
    spec.liquidation_fee_rate = liquidation_fee_rate;
}

void BinanceExchange::clear_symbol_fee_rate_overrides(const std::string& symbol)
{
    runtime_state_->spot_symbol_fee_overrides.erase(symbol);
    runtime_state_->perp_symbol_fee_overrides.erase(symbol);
    const auto symbol_it = step_kernel_state_->symbol_to_id.find(symbol);
    if (symbol_it == step_kernel_state_->symbol_to_id.end()) {
        return;
    }
    const size_t symbol_id = symbol_it->second;
    if (symbol_id >= step_kernel_state_->symbol_spec_by_id.size()) {
        return;
    }
    auto& spec = step_kernel_state_->symbol_spec_by_id[symbol_id];
    if (spec.type == QTrading::Dto::Trading::InstrumentType::Perp) {
        spec.liquidation_fee_rate = -1.0;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
