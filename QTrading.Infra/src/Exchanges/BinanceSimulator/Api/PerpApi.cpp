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
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode)
{
    return Application::OrderCommandKernel(owner_).PlacePerpLimit(
        symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
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
    (void)place_close_position_order(
        symbol,
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Both,
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

double symbol_max_leverage(
    const State::StepKernelState& step_state,
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    double max_allowed = std::numeric_limits<double>::max();

    const auto it = step_state.symbol_to_id.find(symbol);
    if (it != step_state.symbol_to_id.end()) {
        const size_t symbol_id = it->second;
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

    for (const auto& tier : ::margin_tiers) {
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

} // namespace QTrading::Infra::Exchanges::BinanceSim
