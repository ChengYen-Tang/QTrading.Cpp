#include "Exchanges/BinanceSimulator/Domain/LiquidationExecution.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/Domain/LiquidationEligibilityDecision.hpp"
#include "Exchanges/BinanceSimulator/Domain/MaintenanceMarginModel.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr int kMaxLiquidationStepsPerTick = 8;
constexpr double kDefaultWarningMaintenanceMultiplier = 1.0;
constexpr double kDefaultWarningReductionRatio = 0.5;
constexpr double kEpsilon = 1e-12;

double resolve_default_perp_taker_fee_rate(const State::BinanceExchangeRuntimeState& runtime_state) noexcept
{
    auto fee_it = ::vip_fee_rates.find(runtime_state.vip_level);
    if (fee_it == ::vip_fee_rates.end()) {
        fee_it = ::vip_fee_rates.find(0);
    }
    if (fee_it == ::vip_fee_rates.end()) {
        return 0.0;
    }
    return fee_it->second.taker_fee_rate;
}

double resolve_perp_taker_fee_rate(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept
{
    if (symbol_id < step_state.symbols.size()) {
        const auto symbol_it = runtime_state.perp_symbol_fee_overrides.find(step_state.symbols[symbol_id]);
        if (symbol_it != runtime_state.perp_symbol_fee_overrides.end()) {
            return symbol_it->second.taker_fee_rate;
        }
    }
    return resolve_default_perp_taker_fee_rate(runtime_state);
}

double resolve_liquidation_fee_rate(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    size_t symbol_id) noexcept
{
    if (symbol_id < step_state.symbol_spec_by_id.size()) {
        const double symbol_liquidation_fee_rate = step_state.symbol_spec_by_id[symbol_id].liquidation_fee_rate;
        if (symbol_liquidation_fee_rate >= 0.0 && std::isfinite(symbol_liquidation_fee_rate)) {
            return symbol_liquidation_fee_rate;
        }
    }
    return resolve_default_perp_taker_fee_rate(runtime_state);
}

std::optional<size_t> resolve_position_symbol_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const QTrading::dto::Position& position) noexcept
{
    const auto cached_it = runtime_state.position_symbol_id_by_position_id.find(position.id);
    if (cached_it != runtime_state.position_symbol_id_by_position_id.end()) {
        return cached_it->second;
    }
    const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
    if (symbol_it == step_state.symbol_to_id.end()) {
        return std::nullopt;
    }
    runtime_state.position_symbol_id_by_position_id[position.id] = symbol_it->second;
    return symbol_it->second;
}

bool cancel_all_perp_orders(State::BinanceExchangeRuntimeState& runtime_state) noexcept
{
    auto& orders = runtime_state.orders;
    auto& order_symbol_ids = runtime_state.order_symbol_id_by_slot;
    if (order_symbol_ids.size() != orders.size()) {
        order_symbol_ids.assign(orders.size(), std::numeric_limits<size_t>::max());
    }
    size_t write = 0;
    size_t removed_count = 0;
    for (size_t read = 0; read < orders.size(); ++read) {
        auto& order = orders[read];
        const size_t symbol_id = order_symbol_ids[read];
        if (order.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp) {
            runtime_state.order_symbol_id_by_order_id.erase(order.id);
            ++removed_count;
            continue;
        }
        if (write != read) {
            orders[write] = std::move(order);
            order_symbol_ids[write] = symbol_id;
        }
        ++write;
    }
    orders.resize(write);
    order_symbol_ids.resize(write);
    const bool removed = removed_count > 0;
    if (removed) {
        ++runtime_state.orders_version;
    }
    return removed;
}

void append_position_delta(
    const QTrading::dto::Position& position,
    double quantity_before,
    double quantity_closed,
    bool position_closed,
    std::vector<LiquidationPositionDelta>* out_position_deltas) noexcept
{
    if (out_position_deltas == nullptr || !(quantity_closed > kEpsilon)) {
        return;
    }
    LiquidationPositionDelta delta{};
    delta.position_id = position.id;
    delta.symbol = position.symbol;
    delta.instrument_type = position.instrument_type;
    delta.is_long = position.is_long;
    delta.entry_price = position.entry_price;
    delta.quantity_before = quantity_before;
    delta.quantity_closed = quantity_closed;
    delta.position_closed = position_closed;
    out_position_deltas->emplace_back(std::move(delta));
}

void apply_liquidation_wallet_delta(Account& account, double delta) noexcept
{
    if (!(delta < 0.0)) {
        account.apply_perp_wallet_delta(delta);
        return;
    }
    const double wallet_before = account.get_wallet_balance();
    if (!(wallet_before > 0.0)) {
        return;
    }
    const double bounded_delta = std::max(delta, -wallet_before);
    account.apply_perp_wallet_delta(bounded_delta);
}

bool apply_full_liquidation_close_on_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState& step_state,
    const std::vector<double>& mark_price_scratch,
    int position_index,
    std::vector<LiquidationPositionDelta>* out_position_deltas) noexcept
{
    if (position_index < 0 || static_cast<size_t>(position_index) >= runtime_state.positions.size()) {
        return false;
    }

    auto& position = runtime_state.positions[static_cast<size_t>(position_index)];
    if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
        position.quantity <= kEpsilon) {
        return false;
    }
    const auto symbol_id_opt = resolve_position_symbol_id(runtime_state, step_state, position);
    if (!symbol_id_opt.has_value()) {
        return false;
    }
    const size_t symbol_id = *symbol_id_opt;
    if (symbol_id >= mark_price_scratch.size()) {
        return false;
    }
    const double mark = mark_price_scratch[symbol_id];
    if (!(mark > 0.0)) {
        return false;
    }

    const double close_qty = position.quantity;
    const double direction = position.is_long ? 1.0 : -1.0;
    const double realized_pnl = (mark - position.entry_price) * close_qty * direction;
    const double liquidation_fee_rate = resolve_liquidation_fee_rate(runtime_state, step_state, symbol_id);
    const double fee = close_qty * mark * liquidation_fee_rate;
    apply_liquidation_wallet_delta(account, realized_pnl - fee);
    append_position_delta(
        position,
        close_qty,
        close_qty,
        true,
        out_position_deltas);

    runtime_state.positions.erase(
        runtime_state.positions.begin() + static_cast<std::vector<QTrading::dto::Position>::difference_type>(position_index));
    if (static_cast<size_t>(position_index) < runtime_state.position_symbol_id_by_slot.size()) {
        runtime_state.position_symbol_id_by_slot.erase(
            runtime_state.position_symbol_id_by_slot.begin() +
            static_cast<std::vector<size_t>::difference_type>(position_index));
    }
    runtime_state.position_symbol_id_by_position_id.erase(position.id);
    ++runtime_state.positions_version;
    return true;
}

bool apply_warning_zone_partial_reduction_on_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState& step_state,
    const std::vector<double>& mark_price_scratch,
    int position_index,
    double warning_reduction_ratio,
    std::vector<LiquidationPositionDelta>* out_position_deltas) noexcept
{
    if (position_index < 0 || static_cast<size_t>(position_index) >= runtime_state.positions.size()) {
        return false;
    }

    auto& position = runtime_state.positions[static_cast<size_t>(position_index)];
    if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
        position.quantity <= kEpsilon) {
        return false;
    }
    const auto symbol_id_opt = resolve_position_symbol_id(runtime_state, step_state, position);
    if (!symbol_id_opt.has_value()) {
        return false;
    }
    const size_t symbol_id = *symbol_id_opt;
    if (symbol_id >= mark_price_scratch.size()) {
        return false;
    }
    const double mark = mark_price_scratch[symbol_id];
    if (!(mark > 0.0)) {
        return false;
    }

    const double reduce_qty = position.quantity * warning_reduction_ratio;
    if (!(reduce_qty > kEpsilon)) {
        return false;
    }
    const double direction = position.is_long ? 1.0 : -1.0;
    const double realized_pnl = (mark - position.entry_price) * reduce_qty * direction;
    const double taker_fee_rate = resolve_perp_taker_fee_rate(runtime_state, step_state, symbol_id);
    const double fee = reduce_qty * mark * taker_fee_rate;
    apply_liquidation_wallet_delta(account, realized_pnl - fee);

    const double quantity_before = position.quantity;
    position.quantity -= reduce_qty;
    const bool position_closed = position.quantity <= kEpsilon;
    append_position_delta(
        position,
        quantity_before,
        reduce_qty,
        position_closed,
        out_position_deltas);
    if (position.quantity <= kEpsilon) {
        runtime_state.positions.erase(
            runtime_state.positions.begin() + static_cast<std::vector<QTrading::dto::Position>::difference_type>(position_index));
        if (static_cast<size_t>(position_index) < runtime_state.position_symbol_id_by_slot.size()) {
            runtime_state.position_symbol_id_by_slot.erase(
                runtime_state.position_symbol_id_by_slot.begin() +
                static_cast<std::vector<size_t>::difference_type>(position_index));
        }
        runtime_state.position_symbol_id_by_position_id.erase(position.id);
        ++runtime_state.positions_version;
        return true;
    }
    position.notional = std::abs(position.quantity * position.entry_price);
    position.maintenance_margin = ComputeMaintenanceMarginForSymbol(
        std::abs(position.quantity * mark),
        step_state,
        symbol_id);
    ++runtime_state.positions_version;
    return true;
}

} // namespace

bool LiquidationExecution::Run(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload,
    std::vector<LiquidationPositionDelta>* out_position_deltas) noexcept
{
    // Current scope keeps liquidation as direct state reduction:
    // - no synthetic fill external contract is emitted here
    // - no bankruptcy-reset fallback is applied here
    auto& mark_price_scratch = step_state.liquidation_mark_price_scratch;
    auto& has_mark_scratch = step_state.liquidation_has_mark_scratch;

    const auto initial_health = LiquidationEligibilityDecision::Evaluate(
        runtime_state,
        account,
        step_state,
        market_payload,
        mark_price_scratch,
        has_mark_scratch);
    const double warning_maintenance_multiplier = std::max(
        kEpsilon,
        runtime_state.simulation_config.liquidation_warning_maintenance_multiplier > 0.0
            ? runtime_state.simulation_config.liquidation_warning_maintenance_multiplier
            : kDefaultWarningMaintenanceMultiplier);
    const double warning_reduction_ratio = std::clamp(
        runtime_state.simulation_config.liquidation_warning_reduction_ratio > 0.0
            ? runtime_state.simulation_config.liquidation_warning_reduction_ratio
            : kDefaultWarningReductionRatio,
        kEpsilon,
        1.0);
    const bool warning_overlay_enabled =
        warning_maintenance_multiplier > 1.0 + kEpsilon &&
        warning_reduction_ratio > kEpsilon;
    const bool warning_zone = warning_overlay_enabled &&
        initial_health.has_perp_positions &&
        initial_health.has_full_mark_context &&
        initial_health.maintenance_margin > kEpsilon &&
        initial_health.equity + kEpsilon < initial_health.maintenance_margin * warning_maintenance_multiplier;
    if (!initial_health.distressed && !warning_zone) {
        return false;
    }

    bool state_mutated = cancel_all_perp_orders(runtime_state);

    if (!initial_health.distressed) {
        const int warning_idx = initial_health.worst_loss_perp_position_index;
        if (warning_idx < 0) {
            return state_mutated;
        }
        if (apply_warning_zone_partial_reduction_on_position(
                runtime_state,
                account,
                step_state,
                mark_price_scratch,
                warning_idx,
                warning_reduction_ratio,
                out_position_deltas)) {
            state_mutated = true;
        }
        return state_mutated;
    }

    for (int step = 0; step < kMaxLiquidationStepsPerTick; ++step) {
        const auto health = LiquidationEligibilityDecision::Evaluate(
            runtime_state,
            account,
            step_state,
            market_payload,
            mark_price_scratch,
            has_mark_scratch);
        if (!health.distressed) {
            break;
        }
        const int worst_idx = health.worst_loss_perp_position_index;
        if (worst_idx < 0) {
            break;
        }
        if (!apply_full_liquidation_close_on_position(
                runtime_state,
                account,
                step_state,
                mark_price_scratch,
                worst_idx,
                out_position_deltas)) {
            break;
        }
        state_mutated = true;
    }

    return state_mutated;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
