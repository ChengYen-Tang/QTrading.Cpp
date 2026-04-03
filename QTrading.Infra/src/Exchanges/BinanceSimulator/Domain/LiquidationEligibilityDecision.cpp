#include "Exchanges/BinanceSimulator/Domain/LiquidationEligibilityDecision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Domain/MaintenanceMarginModel.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr double kEpsilon = 1e-12;

} // namespace

LiquidationHealthSnapshot LiquidationEligibilityDecision::Evaluate(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Account& account,
    const State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload,
    std::vector<double>& mark_price_scratch,
    std::vector<uint8_t>& has_mark_scratch) noexcept
{
    LiquidationHealthSnapshot out{};
    out.has_full_mark_context = true;

    const size_t symbol_count = step_state.symbols.size();
    mark_price_scratch.assign(symbol_count, 0.0);
    has_mark_scratch.assign(symbol_count, 0);
    const size_t mark_count = std::min(symbol_count, market_payload.mark_klines_by_id.size());
    for (size_t i = 0; i < mark_count; ++i) {
        if (!market_payload.mark_klines_by_id[i].has_value()) {
            continue;
        }
        mark_price_scratch[i] = market_payload.mark_klines_by_id[i]->ClosePrice;
        has_mark_scratch[i] = 1;
    }

    double total_unrealized = 0.0;
    double total_maintenance = 0.0;
    double worst_unrealized = std::numeric_limits<double>::max();
    for (size_t i = 0; i < runtime_state.positions.size(); ++i) {
        const auto& position = runtime_state.positions[i];
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            position.quantity <= kEpsilon) {
            continue;
        }
        out.has_perp_positions = true;
        const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
        if (symbol_it == step_state.symbol_to_id.end()) {
            out.has_full_mark_context = false;
            continue;
        }
        const size_t symbol_id = symbol_it->second;
        if (symbol_id >= has_mark_scratch.size() || has_mark_scratch[symbol_id] == 0) {
            out.has_full_mark_context = false;
            continue;
        }

        const double mark = mark_price_scratch[symbol_id];
        const double direction = position.is_long ? 1.0 : -1.0;
        const double unrealized = (mark - position.entry_price) * position.quantity * direction;
        const double notional = std::abs(position.quantity * mark);
        const double maintenance = ComputeMaintenanceMarginForSymbol(notional, step_state, symbol_id);
        total_unrealized += unrealized;
        total_maintenance += maintenance;
        if (out.worst_loss_perp_position_index < 0 || unrealized < worst_unrealized) {
            out.worst_loss_perp_position_index = static_cast<int>(i);
            worst_unrealized = unrealized;
        }
    }

    const double wallet_balance = account.get_perp_balance().WalletBalance;
    out.equity = wallet_balance + total_unrealized;
    out.maintenance_margin = total_maintenance;
    out.distressed = out.has_perp_positions &&
        out.has_full_mark_context &&
        total_maintenance > kEpsilon &&
        out.equity + kEpsilon < total_maintenance;
    return out;
}

int LiquidationEligibilityDecision::FindWorstLossPerpPositionIndex(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const std::vector<uint8_t>& has_mark_scratch,
    const std::vector<double>& mark_price_scratch) noexcept
{
    int worst_index = -1;
    double worst_unrealized = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(runtime_state.positions.size()); ++i) {
        const auto& position = runtime_state.positions[static_cast<size_t>(i)];
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            position.quantity <= kEpsilon) {
            continue;
        }
        const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
        if (symbol_it == step_state.symbol_to_id.end()) {
            continue;
        }
        const size_t symbol_id = symbol_it->second;
        if (symbol_id >= has_mark_scratch.size() || has_mark_scratch[symbol_id] == 0) {
            continue;
        }
        if (symbol_id >= mark_price_scratch.size()) {
            continue;
        }
        const double mark = mark_price_scratch[symbol_id];
        const double direction = position.is_long ? 1.0 : -1.0;
        const double unrealized = (mark - position.entry_price) * position.quantity * direction;
        if (worst_index < 0 || unrealized < worst_unrealized) {
            worst_index = i;
            worst_unrealized = unrealized;
        }
    }
    return worst_index;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
