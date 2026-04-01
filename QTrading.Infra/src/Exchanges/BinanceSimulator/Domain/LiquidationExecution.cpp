#include "Exchanges/BinanceSimulator/Domain/LiquidationExecution.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Domain/LiquidationEligibilityDecision.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr int kMaxLiquidationStepsPerTick = 8;
constexpr double kLiquidationFeeRate = 0.001;
constexpr double kWarningMaintenanceMultiplier = 2.0;
constexpr double kWarningReductionRatio = 0.5;
constexpr double kEpsilon = 1e-12;
constexpr double kTier1Cap = 50'000.0;
constexpr double kTier1Rate = 0.004;
constexpr double kTier2Rate = 0.005;

double compute_maintenance_margin(double notional) noexcept
{
    if (!(notional > 0.0)) {
        return 0.0;
    }
    if (notional <= kTier1Cap) {
        return notional * kTier1Rate;
    }
    const double bracket_deduction = kTier1Cap * (kTier2Rate - kTier1Rate);
    return (notional * kTier2Rate) - bracket_deduction;
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
    const double fee = close_qty * mark * kLiquidationFeeRate;
    account.apply_perp_wallet_delta(realized_pnl - fee);
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

    const double reduce_qty = position.quantity * kWarningReductionRatio;
    if (!(reduce_qty > kEpsilon)) {
        return false;
    }
    const double direction = position.is_long ? 1.0 : -1.0;
    const double realized_pnl = (mark - position.entry_price) * reduce_qty * direction;
    const double fee = reduce_qty * mark * kLiquidationFeeRate;
    account.apply_perp_wallet_delta(realized_pnl - fee);

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
    position.maintenance_margin = compute_maintenance_margin(std::abs(position.quantity * mark));
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
    const bool warning_zone = initial_health.has_perp_positions &&
        initial_health.has_full_mark_context &&
        initial_health.maintenance_margin > kEpsilon &&
        initial_health.equity + kEpsilon < initial_health.maintenance_margin * kWarningMaintenanceMultiplier;
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
