#include "Exchanges/BinanceSimulator/Domain/LiquidationExecution.hpp"

#include <algorithm>
#include <cmath>

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

bool cancel_all_perp_orders(State::BinanceExchangeRuntimeState& runtime_state) noexcept
{
    const size_t before = runtime_state.orders.size();
    runtime_state.orders.erase(
        std::remove_if(runtime_state.orders.begin(), runtime_state.orders.end(),
            [](const QTrading::dto::Order& order) {
                return order.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp;
            }),
        runtime_state.orders.end());
    return runtime_state.orders.size() != before;
}

bool apply_full_liquidation_close_on_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState& step_state,
    const std::vector<double>& mark_price_scratch,
    int position_index) noexcept
{
    if (position_index < 0 || static_cast<size_t>(position_index) >= runtime_state.positions.size()) {
        return false;
    }

    auto& position = runtime_state.positions[static_cast<size_t>(position_index)];
    if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
        position.quantity <= kEpsilon) {
        return false;
    }
    const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
    if (symbol_it == step_state.symbol_to_id.end()) {
        return false;
    }
    const size_t symbol_id = symbol_it->second;
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

    runtime_state.positions.erase(
        runtime_state.positions.begin() + static_cast<std::vector<QTrading::dto::Position>::difference_type>(position_index));
    return true;
}

bool apply_warning_zone_partial_reduction_on_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState& step_state,
    const std::vector<double>& mark_price_scratch,
    int position_index) noexcept
{
    if (position_index < 0 || static_cast<size_t>(position_index) >= runtime_state.positions.size()) {
        return false;
    }

    auto& position = runtime_state.positions[static_cast<size_t>(position_index)];
    if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
        position.quantity <= kEpsilon) {
        return false;
    }
    const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
    if (symbol_it == step_state.symbol_to_id.end()) {
        return false;
    }
    const size_t symbol_id = symbol_it->second;
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

    position.quantity -= reduce_qty;
    if (position.quantity <= kEpsilon) {
        runtime_state.positions.erase(
            runtime_state.positions.begin() + static_cast<std::vector<QTrading::dto::Position>::difference_type>(position_index));
        return true;
    }
    position.notional = std::abs(position.quantity * position.entry_price);
    position.maintenance_margin = compute_maintenance_margin(std::abs(position.quantity * mark));
    return true;
}

} // namespace

bool LiquidationExecution::Run(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload) noexcept
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
        const int warning_idx = LiquidationEligibilityDecision::FindWorstLossPerpPositionIndex(
            runtime_state,
            step_state,
            has_mark_scratch,
            mark_price_scratch);
        if (warning_idx < 0) {
            return state_mutated;
        }
        if (apply_warning_zone_partial_reduction_on_position(
                runtime_state,
                account,
                step_state,
                mark_price_scratch,
                warning_idx)) {
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
        const int worst_idx = LiquidationEligibilityDecision::FindWorstLossPerpPositionIndex(
            runtime_state,
            step_state,
            has_mark_scratch,
            mark_price_scratch);
        if (worst_idx < 0) {
            break;
        }
        if (!apply_full_liquidation_close_on_position(
                runtime_state,
                account,
                step_state,
                mark_price_scratch,
                worst_idx)) {
            break;
        }
        state_mutated = true;
    }

    return state_mutated;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
