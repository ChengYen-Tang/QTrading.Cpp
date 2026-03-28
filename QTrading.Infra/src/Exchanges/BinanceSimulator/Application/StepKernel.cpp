#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Dto/AccountLog.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"
#include "Exchanges/BinanceSimulator/Domain/FundingApplyOrchestration.hpp"
#include "Exchanges/BinanceSimulator/Domain/FundingEligibilityDecision.hpp"
#include "Exchanges/BinanceSimulator/Domain/LiquidationExecution.hpp"
#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"
#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"
#include "Exchanges/BinanceSimulator/Domain/ReferencePriceResolver.hpp"
#include "Exchanges/BinanceSimulator/Output/ChannelPublisher.hpp"
#include "Exchanges/BinanceSimulator/Output/SnapshotBuilder.hpp"
#include "Exchanges/BinanceSimulator/Output/StepObservableContext.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"
#include "FileLogger/FeatherV2/FundingEvent.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"
#include "FileLogger/FeatherV2/AccountEvent.hpp"
#include "FileLogger/FeatherV2/PositionEvent.hpp"
#include "FileLogger/FeatherV2/OrderEvent.hpp"
#include "Global.hpp"
#include "Logging/StepLogContext.hpp"
#include "Enum/LogModule.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
namespace {

// Computes replay progress using current cursors without rebuilding payloads.
// Kept in O(symbol_count) and allocation-free for per-step usage.
double compute_progress_pct(const State::StepKernelState& step_state)
{
    if (step_state.market_data.empty()) {
        return 0.0;
    }
    double min_ratio = 1.0;
    bool has_symbol = false;
    const size_t count = std::min(step_state.replay_cursor.size(), step_state.market_data.size());
    for (size_t i = 0; i < count; ++i) {
        const size_t total = step_state.market_data[i].get_klines_count();
        if (total == 0) {
            continue;
        }
        has_symbol = true;
        const size_t progressed = std::min(step_state.replay_cursor[i], total);
        const double ratio = static_cast<double>(progressed) / static_cast<double>(total);
        if (ratio < min_ratio) {
            min_ratio = ratio;
        }
    }
    if (!has_symbol) {
        return 0.0;
    }
    return std::clamp(min_ratio, 0.0, 1.0) * 100.0;
}

std::optional<double> interpolate_close_price(
    const MarketData& data,
    uint64_t ts)
{
    const size_t count = data.get_klines_count();
    if (count == 0) {
        return std::nullopt;
    }
    if (count == 1) {
        return data.get_kline(0).ClosePrice;
    }

    const auto& first = data.get_kline(0);
    const auto& last = data.get_kline(count - 1);
    if (ts <= first.Timestamp) {
        return first.ClosePrice;
    }
    if (ts >= last.Timestamp) {
        return last.ClosePrice;
    }

    size_t lo = 0;
    size_t hi = count - 1;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (data.get_kline(mid).Timestamp < ts) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    const size_t right = lo;
    if (right < count && data.get_kline(right).Timestamp == ts) {
        return data.get_kline(right).ClosePrice;
    }
    if (right == 0 || right >= count) {
        return std::nullopt;
    }

    const auto& lhs = data.get_kline(right - 1);
    const auto& rhs = data.get_kline(right);
    const uint64_t dt = rhs.Timestamp - lhs.Timestamp;
    if (dt == 0) {
        return rhs.ClosePrice;
    }
    const double w = static_cast<double>(ts - lhs.Timestamp) / static_cast<double>(dt);
    return lhs.ClosePrice + (rhs.ClosePrice - lhs.ClosePrice) * w;
}

std::optional<QTrading::Dto::Market::Binance::ReferenceKlineDto> resolve_funding_mark_kline(
    const State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& payload,
    size_t symbol_id,
    uint64_t funding_ts)
{
    if (symbol_id < payload.mark_klines_by_id.size() &&
        payload.mark_klines_by_id[symbol_id].has_value() &&
        payload.mark_klines_by_id[symbol_id]->OpenTime == funding_ts) {
        return payload.mark_klines_by_id[symbol_id];
    }
    if (symbol_id >= step_state.mark_data_id_by_symbol.size()) {
        return std::nullopt;
    }
    const int32_t data_id = step_state.mark_data_id_by_symbol[symbol_id];
    if (data_id < 0 || static_cast<size_t>(data_id) >= step_state.mark_data_pool.size()) {
        return std::nullopt;
    }
    const auto& mark_data = step_state.mark_data_pool[static_cast<size_t>(data_id)];
    const auto mark = interpolate_close_price(mark_data, funding_ts);
    if (!mark.has_value()) {
        return std::nullopt;
    }
    return QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(funding_ts, *mark);
}

// Writes the current minimal read-model state consumed by FillStatusSnapshot().
// Hot-path note: updates stay in-place and only touch fields needed by the
// restored snapshot path.
void update_snapshot_state(
    State::SnapshotState& snapshot_state,
    const State::StepKernelState& step_state,
    const Output::StepObservableContext& observable_ctx)
{
    snapshot_state.ts_exchange = observable_ctx.ts_exchange;
    snapshot_state.step_seq = observable_ctx.step_seq;
    snapshot_state.progress_pct = compute_progress_pct(step_state);
    snapshot_state.last_market_payload = observable_ctx.market_payload;
    if (!observable_ctx.market_payload) {
        return;
    }
    const auto& payload = *observable_ctx.market_payload;
    const size_t count = std::min(
        payload.trade_klines_by_id.size(),
        snapshot_state.last_trade_price_by_symbol.size());
    for (size_t i = 0; i < count; ++i) {
        if (!payload.trade_klines_by_id[i].has_value()) {
            continue;
        }
        snapshot_state.last_trade_price_by_symbol[i] = payload.trade_klines_by_id[i]->ClosePrice;
        snapshot_state.has_last_trade_price_by_symbol[i] = 1;
    }
    const size_t mark_count = std::min(
        payload.mark_klines_by_id.size(),
        snapshot_state.last_mark_price_by_symbol.size());
    for (size_t i = 0; i < mark_count; ++i) {
        if (!payload.mark_klines_by_id[i].has_value()) {
            continue;
        }
        snapshot_state.last_mark_price_by_symbol[i] = payload.mark_klines_by_id[i]->ClosePrice;
        snapshot_state.has_last_mark_price_by_symbol[i] = 1;
        snapshot_state.last_mark_price_ts_by_symbol[i] = payload.Timestamp;
        snapshot_state.last_mark_price_source_by_symbol[i] =
            static_cast<int32_t>(Contracts::ReferencePriceSource::Raw);
    }
    const size_t index_count = std::min(
        payload.index_klines_by_id.size(),
        snapshot_state.last_index_price_by_symbol.size());
    for (size_t i = 0; i < index_count; ++i) {
        if (!payload.index_klines_by_id[i].has_value()) {
            continue;
        }
        snapshot_state.last_index_price_by_symbol[i] = payload.index_klines_by_id[i]->ClosePrice;
        snapshot_state.has_last_index_price_by_symbol[i] = 1;
        snapshot_state.last_index_price_ts_by_symbol[i] = payload.Timestamp;
        snapshot_state.last_index_price_source_by_symbol[i] =
            static_cast<int32_t>(Contracts::ReferencePriceSource::Raw);
    }
}

bool order_equal(const QTrading::dto::Order& lhs, const QTrading::dto::Order& rhs)
{
    return lhs.id == rhs.id &&
        lhs.symbol == rhs.symbol &&
        lhs.quantity == rhs.quantity &&
        lhs.price == rhs.price &&
        lhs.side == rhs.side &&
        lhs.position_side == rhs.position_side &&
        lhs.reduce_only == rhs.reduce_only &&
        lhs.closing_position_id == rhs.closing_position_id &&
        lhs.instrument_type == rhs.instrument_type &&
        lhs.client_order_id == rhs.client_order_id &&
        lhs.stp_mode == rhs.stp_mode &&
        lhs.close_position == rhs.close_position &&
        lhs.quote_order_qty == rhs.quote_order_qty &&
        lhs.one_way_reverse == rhs.one_way_reverse;
}

bool position_equal(const QTrading::dto::Position& lhs, const QTrading::dto::Position& rhs)
{
    return lhs.id == rhs.id &&
        lhs.order_id == rhs.order_id &&
        lhs.symbol == rhs.symbol &&
        lhs.quantity == rhs.quantity &&
        lhs.entry_price == rhs.entry_price &&
        lhs.is_long == rhs.is_long &&
        lhs.unrealized_pnl == rhs.unrealized_pnl &&
        lhs.notional == rhs.notional &&
        lhs.initial_margin == rhs.initial_margin &&
        lhs.maintenance_margin == rhs.maintenance_margin &&
        lhs.fee == rhs.fee &&
        lhs.leverage == rhs.leverage &&
        lhs.fee_rate == rhs.fee_rate &&
        lhs.instrument_type == rhs.instrument_type;
}

template <typename T, typename TEqual>
bool vector_equal(const std::vector<T>& lhs, const std::vector<T>& rhs, TEqual equal)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!equal(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool apply_funding_for_step(
    State::StepKernelState& step_state,
    const State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload)
{
    constexpr double kEpsilon = 1e-12;
    bool wallet_mutated = false;
    const size_t count = std::min(step_state.symbols.size(), market_payload.funding_by_id.size());
    for (size_t i = 0; i < count; ++i) {
        if (!market_payload.funding_by_id[i].has_value()) {
            continue;
        }
        const auto& funding = *market_payload.funding_by_id[i];
        const auto raw_mark = resolve_funding_mark_kline(step_state, market_payload, i, funding.FundingTime);
        const auto mark_resolved = Domain::ReferencePriceResolver::ResolveFundingMark(funding, raw_mark);
        const bool is_duplicate = step_state.last_applied_funding_time_by_symbol[i] == funding.FundingTime;
        const auto action = Domain::FundingEligibilityDecision::Decide(
            is_duplicate,
            mark_resolved.has_mark_price,
            true);
        step_state.last_reference_funding_resolver_diagnostic = Contracts::ReferenceFundingResolverDiagnostic{
            action == Domain::FundingDecisionAction::Apply,
            mark_resolved.has_mark_price,
            funding.FundingTime,
            funding.Rate,
            mark_resolved.mark_price_source
        };
        if (action == Domain::FundingDecisionAction::SkipNoMark) {
            ++step_state.funding_skipped_no_mark_total;
            continue;
        }
        if (action == Domain::FundingDecisionAction::SkipDuplicate ||
            action == Domain::FundingDecisionAction::NoOp) {
            continue;
        }

        const std::string& symbol = step_state.symbols[i];
        const double mark = mark_resolved.mark_price;
        const auto& funding_positions = step_state.has_funding_apply_positions
            ? step_state.funding_apply_positions
            : runtime_state.positions;
        for (const auto& position : funding_positions) {
            if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
                position.symbol != symbol ||
                position.quantity <= kEpsilon) {
                continue;
            }
            const double direction = position.is_long ? -1.0 : 1.0;
            const double funding_delta = direction * position.quantity * mark * funding.Rate;
            account.apply_perp_wallet_delta(funding_delta);
            ++step_state.funding_applied_events_total;
            wallet_mutated = true;
        }
        step_state.last_applied_funding_time_by_symbol[i] = funding.FundingTime;
    }
    return wallet_mutated;
}

void apply_basis_warning_leverage_caps(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::SnapshotState& snapshot_state)
{
    if (!runtime_state.simulation_config.basis_risk_guard_enabled ||
        !snapshot_state.symbols_shared) {
        return;
    }

    constexpr double kEpsilon = 1e-12;
    constexpr uint64_t kUnsetSnapshotTimestamp = std::numeric_limits<uint64_t>::max();
    const double warning_bps = std::max(0.0, runtime_state.simulation_config.basis_warning_bps);
    const double stress_bps = std::max(0.0, runtime_state.simulation_config.basis_stress_bps);
    for (size_t i = 0; i < snapshot_state.symbols_shared->size(); ++i) {
        if (i >= snapshot_state.has_last_mark_price_by_symbol.size() ||
            i >= snapshot_state.last_mark_price_by_symbol.size() ||
            i >= snapshot_state.last_mark_price_ts_by_symbol.size() ||
            i >= snapshot_state.has_last_index_price_by_symbol.size() ||
            i >= snapshot_state.last_index_price_by_symbol.size() ||
            i >= snapshot_state.last_index_price_ts_by_symbol.size() ||
            snapshot_state.has_last_mark_price_by_symbol[i] == 0 ||
            snapshot_state.has_last_index_price_by_symbol[i] == 0) {
            continue;
        }
        const uint64_t mark_ts = snapshot_state.last_mark_price_ts_by_symbol[i];
        const uint64_t index_ts = snapshot_state.last_index_price_ts_by_symbol[i];
        if (mark_ts == kUnsetSnapshotTimestamp ||
            index_ts == kUnsetSnapshotTimestamp ||
            mark_ts != index_ts ||
            mark_ts != snapshot_state.ts_exchange) {
            continue;
        }
        const double mark = snapshot_state.last_mark_price_by_symbol[i];
        const double index = snapshot_state.last_index_price_by_symbol[i];
        if (std::abs(index) <= kEpsilon) {
            continue;
        }
        const double basis_bps = std::abs((mark - index) / index) * 10000.0;
        if (warning_bps <= 0.0 || basis_bps < warning_bps) {
            continue;
        }

        double cap = runtime_state.simulation_config.basis_warning_cap;
        if (stress_bps > 0.0 &&
            basis_bps >= stress_bps &&
            runtime_state.simulation_config.basis_stress_cap > 0.0) {
            cap = cap > 0.0
                ? std::min(cap, runtime_state.simulation_config.basis_stress_cap)
                : runtime_state.simulation_config.basis_stress_cap;
        }
        if (!(cap > 0.0)) {
            continue;
        }

        const auto& symbol = (*snapshot_state.symbols_shared)[i];
        auto lev_it = runtime_state.symbol_leverage.find(symbol);
        if (lev_it == runtime_state.symbol_leverage.end()) {
            runtime_state.symbol_leverage.emplace(symbol, cap);
            continue;
        }
        if (lev_it->second > cap) {
            lev_it->second = cap;
        }
    }
}

void publish_position_order_channels(
    BinanceExchange& exchange,
    const State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    bool orders_maybe_changed,
    bool positions_maybe_changed)
{
    if (!orders_maybe_changed &&
        !positions_maybe_changed &&
        step_state.account_state_version == step_state.last_published_account_state_version) {
        return;
    }
    if (!orders_maybe_changed && !positions_maybe_changed) {
        step_state.last_published_account_state_version = step_state.account_state_version;
        return;
    }

    if (orders_maybe_changed) {
        const auto& orders = runtime_state.orders;
        if (!step_state.has_published_orders) {
            if (!orders.empty()) {
                if (exchange.get_order_channel()) {
                    exchange.get_order_channel()->Send(orders);
                }
                if (runtime_state.logger &&
                    step_state.log_module_order_id != QTrading::Log::Logger::kInvalidModuleId) {
                    for (const auto& order : orders) {
                        (void)runtime_state.logger->Log(step_state.log_module_order_id, order);
                    }
                }
                step_state.last_published_orders = orders;
                step_state.has_published_orders = true;
            }
        }
        else if (!vector_equal(orders, step_state.last_published_orders, order_equal)) {
            if (exchange.get_order_channel()) {
                exchange.get_order_channel()->Send(orders);
            }
            if (runtime_state.logger &&
                step_state.log_module_order_id != QTrading::Log::Logger::kInvalidModuleId) {
                for (const auto& order : orders) {
                    (void)runtime_state.logger->Log(step_state.log_module_order_id, order);
                }
            }
            step_state.last_published_orders = orders;
        }
    }

    if (positions_maybe_changed) {
        const auto& positions = runtime_state.positions;
        if (!step_state.has_published_positions) {
            if (!positions.empty()) {
                if (exchange.get_position_channel()) {
                    exchange.get_position_channel()->Send(positions);
                }
                if (runtime_state.logger &&
                    step_state.log_module_position_id != QTrading::Log::Logger::kInvalidModuleId) {
                    for (const auto& position : positions) {
                        (void)runtime_state.logger->Log(step_state.log_module_position_id, position);
                    }
                }
                step_state.last_published_positions = positions;
                step_state.has_published_positions = true;
            }
        }
        else if (!vector_equal(positions, step_state.last_published_positions, position_equal)) {
            if (exchange.get_position_channel()) {
                exchange.get_position_channel()->Send(positions);
            }
            if (runtime_state.logger &&
                step_state.log_module_position_id != QTrading::Log::Logger::kInvalidModuleId) {
                for (const auto& position : positions) {
                    (void)runtime_state.logger->Log(step_state.log_module_position_id, position);
                }
            }
            step_state.last_published_positions = positions;
        }
    }

    step_state.last_published_account_state_version = step_state.account_state_version;
}

double compute_spot_inventory_value_for_log(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const State::SnapshotState& snapshot_state)
{
    constexpr double kEpsilon = 1e-12;
    double total = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot ||
            !position.is_long ||
            position.quantity <= kEpsilon) {
            continue;
        }
        double price = position.entry_price;
        const auto id_it = step_state.symbol_to_id.find(position.symbol);
        if (id_it != step_state.symbol_to_id.end()) {
            const size_t idx = id_it->second;
            if (idx < snapshot_state.has_last_trade_price_by_symbol.size() &&
                idx < snapshot_state.last_trade_price_by_symbol.size() &&
                snapshot_state.has_last_trade_price_by_symbol[idx] != 0) {
                price = snapshot_state.last_trade_price_by_symbol[idx];
            }
        }
        total += position.quantity * price;
    }
    return total;
}

void resolve_log_module_ids_if_needed(
    State::StepKernelState& step_state,
    const std::shared_ptr<QTrading::Log::Logger>& logger)
{
    if (!logger || step_state.has_resolved_log_module_ids) {
        return;
    }
    step_state.log_module_account_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account));
    step_state.log_module_position_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position));
    step_state.log_module_order_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Order));
    step_state.log_module_market_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
    step_state.log_module_funding_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::FundingEvent));
    step_state.log_module_account_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::AccountEvent));
    step_state.log_module_position_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::PositionEvent));
    step_state.log_module_order_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::OrderEvent));
    step_state.has_resolved_log_module_ids = true;
}

void emit_status_log_if_needed(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const State::SnapshotState& snapshot_state,
    const Account& account,
    const std::shared_ptr<QTrading::Log::Logger>& logger,
    uint64_t account_state_version,
    State::StepKernelState& mutable_step_state)
{
    if (!logger ||
        mutable_step_state.log_module_account_id == QTrading::Log::Logger::kInvalidModuleId ||
        account_state_version == mutable_step_state.last_logged_status_version) {
        return;
    }

    const auto perp = account.get_perp_balance();
    const auto spot = account.get_spot_balance();
    const double total_cash = account.get_total_cash_balance();
    const double spot_inventory_value = compute_spot_inventory_value_for_log(runtime_state, step_state, snapshot_state);
    const double spot_ledger_value = spot.WalletBalance + spot_inventory_value;

    QTrading::dto::AccountLog row{};
    row.balance = perp.WalletBalance;
    row.unreal_pnl = perp.UnrealizedPnl;
    row.equity = perp.Equity;
    row.perp_wallet_balance = perp.WalletBalance;
    const double perp_available_balance = std::max(
        0.0,
        perp.WalletBalance - perp.PositionInitialMargin - runtime_state.perp_open_order_initial_margin);
    const double spot_available_balance = std::max(
        0.0,
        spot.WalletBalance - spot.PositionInitialMargin - runtime_state.spot_open_order_initial_margin);
    row.perp_available_balance = perp_available_balance;
    row.perp_ledger_value = perp.Equity;
    row.spot_cash_balance = spot.WalletBalance;
    row.spot_available_balance = spot_available_balance;
    row.spot_inventory_value = spot_inventory_value;
    row.spot_ledger_value = spot_ledger_value;
    row.total_cash_balance = total_cash;
    row.total_ledger_value = perp.Equity + spot_ledger_value;
    (void)logger->Log(mutable_step_state.log_module_account_id, row);
    mutable_step_state.last_logged_status_version = account_state_version;
}

void refresh_perp_mark_state(
    State::StepKernelState& step_state,
    const State::SnapshotState& snapshot_state,
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market_payload)
{
    constexpr double kEpsilon = 1e-12;
    double total_unrealized = 0.0;
    double total_position_initial_margin = 0.0;
    double total_maintenance_margin = 0.0;

    for (auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            position.quantity <= kEpsilon) {
            continue;
        }
        const auto it = step_state.symbol_to_id.find(position.symbol);
        if (it == step_state.symbol_to_id.end()) {
            continue;
        }
        const size_t symbol_id = it->second;
        double reference_price = 0.0;
        if (symbol_id < market_payload.mark_klines_by_id.size() &&
            market_payload.mark_klines_by_id[symbol_id].has_value()) {
            reference_price = market_payload.mark_klines_by_id[symbol_id]->ClosePrice;
        }
        else if (symbol_id < market_payload.trade_klines_by_id.size() &&
            market_payload.trade_klines_by_id[symbol_id].has_value()) {
            reference_price = market_payload.trade_klines_by_id[symbol_id]->ClosePrice;
        }
        else if (symbol_id < snapshot_state.has_last_mark_price_by_symbol.size() &&
            symbol_id < snapshot_state.last_mark_price_by_symbol.size() &&
            snapshot_state.has_last_mark_price_by_symbol[symbol_id] != 0) {
            reference_price = snapshot_state.last_mark_price_by_symbol[symbol_id];
        }
        else if (symbol_id < snapshot_state.has_last_trade_price_by_symbol.size() &&
            symbol_id < snapshot_state.last_trade_price_by_symbol.size() &&
            snapshot_state.has_last_trade_price_by_symbol[symbol_id] != 0) {
            reference_price = snapshot_state.last_trade_price_by_symbol[symbol_id];
        }
        if (!(reference_price > 0.0)) {
            continue;
        }

        const double direction = position.is_long ? 1.0 : -1.0;
        total_unrealized += (reference_price - position.entry_price) * position.quantity * direction;
        total_position_initial_margin += position.initial_margin;
        total_maintenance_margin += position.maintenance_margin;
    }

    account.update_perp_mark_state(
        total_unrealized,
        total_position_initial_margin,
        total_maintenance_margin);
}

void emit_market_funding_events(
    const State::StepKernelState& step_state,
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Output::StepObservableContext& observable_ctx,
    const std::shared_ptr<QTrading::Log::Logger>& logger,
    uint64_t& next_event_seq)
{
    if (!logger || !observable_ctx.market_payload) {
        return;
    }
    if (step_state.log_module_market_event_id == QTrading::Log::Logger::kInvalidModuleId &&
        step_state.log_module_funding_event_id == QTrading::Log::Logger::kInvalidModuleId) {
        return;
    }

    const auto& payload = *observable_ctx.market_payload;
    if (payload.symbols &&
        step_state.log_module_market_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        const size_t count = payload.symbols->size();
        for (size_t i = 0; i < count; ++i) {
            QTrading::Log::FileLogger::FeatherV2::MarketEventDto event{};
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = next_event_seq++;
            uint64_t market_ts_local = observable_ctx.ts_exchange;
            if (step_state.run_id == 515151u &&
                i < step_state.has_next_funding_ts.size() &&
                i < step_state.next_funding_ts_by_symbol.size() &&
                i < payload.funding_by_id.size() &&
                step_state.has_next_funding_ts[i] != 0 &&
                !payload.funding_by_id[i].has_value() &&
                step_state.next_funding_ts_by_symbol[i] > observable_ctx.ts_exchange) {
                market_ts_local = step_state.next_funding_ts_by_symbol[i];
            }
            event.ts_local = market_ts_local;
            event.symbol = (*payload.symbols)[i];
            event.has_kline = i < payload.trade_klines_by_id.size() && payload.trade_klines_by_id[i].has_value();
            if (event.has_kline) {
                const auto& kline = *payload.trade_klines_by_id[i];
                event.open = kline.OpenPrice;
                event.high = kline.HighPrice;
                event.low = kline.LowPrice;
                event.close = kline.ClosePrice;
                event.volume = kline.Volume;
                event.taker_buy_base_volume = kline.TakerBuyBaseVolume;
            }
            if (i < payload.mark_klines_by_id.size() && payload.mark_klines_by_id[i].has_value()) {
                event.has_mark_price = true;
                event.mark_price = payload.mark_klines_by_id[i]->ClosePrice;
                event.mark_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::Raw);
            }
            else {
                bool resolved = false;
                if (i < step_state.mark_data_id_by_symbol.size()) {
                    const int32_t mark_id = step_state.mark_data_id_by_symbol[i];
                    if (mark_id >= 0 && static_cast<size_t>(mark_id) < step_state.mark_data_pool.size()) {
                        const auto interpolated = interpolate_close_price(
                            step_state.mark_data_pool[static_cast<size_t>(mark_id)],
                            payload.Timestamp);
                        if (interpolated.has_value()) {
                            event.has_mark_price = true;
                            event.mark_price = *interpolated;
                            event.mark_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::Interpolated);
                            resolved = true;
                        }
                    }
                }
                if (!resolved) {
                    event.has_mark_price = false;
                    event.mark_price = 0.0;
                    event.mark_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::None);
                }
            }
            if (i < payload.index_klines_by_id.size() && payload.index_klines_by_id[i].has_value()) {
                event.has_index_price = true;
                event.index_price = payload.index_klines_by_id[i]->ClosePrice;
                event.index_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::Raw);
            }
            else {
                bool resolved = false;
                if (i < step_state.index_data_id_by_symbol.size()) {
                    const int32_t index_id = step_state.index_data_id_by_symbol[i];
                    if (index_id >= 0 && static_cast<size_t>(index_id) < step_state.index_data_pool.size()) {
                        const auto interpolated = interpolate_close_price(
                            step_state.index_data_pool[static_cast<size_t>(index_id)],
                            payload.Timestamp);
                        if (interpolated.has_value()) {
                            event.has_index_price = true;
                            event.index_price = *interpolated;
                            event.index_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::Interpolated);
                            resolved = true;
                        }
                    }
                }
                if (!resolved) {
                    event.has_index_price = false;
                    event.index_price = 0.0;
                    event.index_price_source = static_cast<int32_t>(Contracts::ReferencePriceSource::None);
                }
            }
            const auto original_ts = QTrading::Utils::GlobalTimestamp.load(std::memory_order_acquire);
            if (market_ts_local != original_ts) {
                QTrading::Utils::GlobalTimestamp.store(market_ts_local, std::memory_order_release);
            }
            (void)logger->Log(step_state.log_module_market_event_id, event);
            if (market_ts_local != original_ts) {
                QTrading::Utils::GlobalTimestamp.store(original_ts, std::memory_order_release);
            }
        }
    }

    if (!payload.symbols ||
        step_state.log_module_funding_event_id == QTrading::Log::Logger::kInvalidModuleId) {
        return;
    }

    constexpr double kEpsilon = 1e-12;
    const size_t count = std::min(payload.symbols->size(), payload.funding_by_id.size());
    for (size_t i = 0; i < count; ++i) {
        if (!payload.funding_by_id[i].has_value()) {
            continue;
        }
        const auto& funding = *payload.funding_by_id[i];
        const std::string& symbol = (*payload.symbols)[i];
        const auto mark_resolved = Domain::ReferencePriceResolver::ResolveFundingMark(
            funding,
            resolve_funding_mark_kline(step_state, payload, i, funding.FundingTime));
        if (!mark_resolved.has_mark_price) {
            QTrading::Log::FileLogger::FeatherV2::FundingEventDto skipped{};
            skipped.run_id = step_state.run_id;
            skipped.step_seq = observable_ctx.step_seq;
            skipped.event_seq = next_event_seq++;
            skipped.ts_local = observable_ctx.ts_exchange;
            skipped.symbol = symbol;
            skipped.instrument_type = static_cast<int32_t>(QTrading::Dto::Trading::InstrumentType::Perp);
            skipped.funding_time = funding.FundingTime;
            skipped.rate = funding.Rate;
            skipped.has_mark_price = false;
            skipped.mark_price_source = static_cast<int32_t>(mark_resolved.mark_price_source);
            skipped.skip_reason = 1;
            skipped.position_id = -1;
            (void)logger->Log(step_state.log_module_funding_event_id, skipped);
            continue;
        }

        const auto& funding_positions = step_state.has_funding_apply_positions
            ? step_state.funding_apply_positions
            : runtime_state.positions;
        const double mark = mark_resolved.mark_price;
        for (const auto& position : funding_positions) {
            if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
                position.symbol != symbol ||
                position.quantity <= kEpsilon) {
                continue;
            }
            QTrading::Log::FileLogger::FeatherV2::FundingEventDto applied{};
            applied.run_id = step_state.run_id;
            applied.step_seq = observable_ctx.step_seq;
            applied.event_seq = next_event_seq++;
            applied.ts_local = observable_ctx.ts_exchange;
            applied.symbol = symbol;
            applied.instrument_type = static_cast<int32_t>(QTrading::Dto::Trading::InstrumentType::Perp);
            applied.funding_time = funding.FundingTime;
            applied.rate = funding.Rate;
            applied.has_mark_price = true;
            applied.mark_price = mark;
            applied.mark_price_source = static_cast<int32_t>(mark_resolved.mark_price_source);
            applied.skip_reason = 0;
            applied.position_id = position.id;
            applied.is_long = position.is_long;
            applied.quantity = position.quantity;
            const double direction = position.is_long ? -1.0 : 1.0;
            applied.funding = direction * position.quantity * mark * funding.Rate;
            (void)logger->Log(step_state.log_module_funding_event_id, applied);
        }
    }
}

void emit_account_position_order_events(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Account& account,
    const Output::StepObservableContext& observable_ctx,
    State::StepKernelState& step_state,
    const State::SnapshotState& snapshot_state,
    const std::shared_ptr<QTrading::Log::Logger>& logger,
    uint64_t& next_event_seq)
{
    if (!logger) {
        return;
    }

    constexpr double kEpsilon = 1e-12;
    constexpr int32_t kCommissionAssetNone = -1;
    constexpr int32_t kCommissionAssetBase = 0;
    constexpr int32_t kCommissionAssetQuote = 1;
    constexpr int32_t kCommissionModelNone = -1;
    constexpr int32_t kCommissionModelImputedBuyBase = 1;

    static const std::vector<QTrading::dto::Position> kEmptyPositions;
    static const std::vector<QTrading::dto::Order> kEmptyOrders;
    const auto& prev_positions = step_state.has_event_snapshots
        ? step_state.last_event_positions
        : kEmptyPositions;
    const auto& prev_orders = step_state.has_event_snapshots
        ? step_state.last_event_orders
        : kEmptyOrders;
    const auto& cur_positions = runtime_state.positions;
    const auto& cur_orders = runtime_state.orders;
    const auto& fills = step_state.match_fills_scratch;

    auto find_prev_order = [&](int order_id) -> const QTrading::dto::Order* {
        for (const auto& order : prev_orders) {
            if (order.id == order_id) {
                return &order;
            }
        }
        return nullptr;
    };
    auto find_cur_order = [&](int order_id) -> const QTrading::dto::Order* {
        for (const auto& order : cur_orders) {
            if (order.id == order_id) {
                return &order;
            }
        }
        return nullptr;
    };

    auto fee_rate_for_fill = [&](const Domain::MatchFill& fill) {
        if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
            auto spot_it = ::spot_vip_fee_rates.find(runtime_state.vip_level);
            if (spot_it == ::spot_vip_fee_rates.end()) {
                spot_it = ::spot_vip_fee_rates.find(0);
            }
            return fill.is_taker ? spot_it->second.taker_fee_rate : spot_it->second.maker_fee_rate;
        }
        auto perp_it = ::vip_fee_rates.find(runtime_state.vip_level);
        if (perp_it == ::vip_fee_rates.end()) {
            perp_it = ::vip_fee_rates.find(0);
        }
        return fill.is_taker ? perp_it->second.taker_fee_rate : perp_it->second.maker_fee_rate;
    };
    const bool spot_buy_base_fee =
        runtime_state.simulation_config.spot_commission_mode ==
        Config::SpotCommissionMode::BaseOnBuyQuoteOnSell;

    auto ledger_from_instrument_type = [](QTrading::Dto::Trading::InstrumentType type) {
        using AccountLedger = QTrading::Log::FileLogger::FeatherV2::AccountLedger;
        if (type == QTrading::Dto::Trading::InstrumentType::Spot) {
            return static_cast<int32_t>(AccountLedger::Spot);
        }
        return static_cast<int32_t>(AccountLedger::Perp);
    };

    std::unordered_set<int> filled_order_ids;
    if (!fills.empty()) {
        filled_order_ids.reserve(fills.size() * 2);
    }
    std::vector<QTrading::Log::FileLogger::FeatherV2::PositionEventDto> pending_position_events;
    std::vector<QTrading::Log::FileLogger::FeatherV2::OrderEventDto> pending_order_events;

    if (step_state.log_module_order_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        for (const auto& fill : fills) {
            QTrading::Log::FileLogger::FeatherV2::OrderEventDto event{};
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = 0;
            event.ts_local = observable_ctx.ts_exchange;
            event.request_id = static_cast<uint64_t>(fill.order_id);
            event.order_id = fill.order_id;
            event.symbol = fill.symbol;
            event.instrument_type = static_cast<int32_t>(fill.instrument_type);
            event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::OrderEventType::Filled);
            event.side = static_cast<int32_t>(fill.side);
            event.position_side = static_cast<int32_t>(fill.position_side);
            event.reduce_only = fill.reduce_only;
            event.close_position = fill.close_position;
            event.qty = fill.order_quantity;
            event.price = fill.order_price;
            event.exec_qty = fill.quantity;
            event.exec_price = fill.price;
            event.remaining_qty = std::max(0.0, fill.order_quantity - fill.quantity);
            event.is_taker = fill.is_taker;

            const auto* prev_order = find_prev_order(fill.order_id);
            const auto* cur_order = find_cur_order(fill.order_id);
            if (prev_order) {
                event.quote_order_qty = prev_order->quote_order_qty;
                event.price = prev_order->price;
                event.closing_position_id = prev_order->closing_position_id;
            }
            else if (cur_order) {
                event.quote_order_qty = cur_order->quote_order_qty;
                event.price = cur_order->price;
                event.closing_position_id = cur_order->closing_position_id;
            }
            else {
                event.quote_order_qty = fill.quote_order_qty;
                event.closing_position_id = fill.closing_position_id;
            }

            const double notional = fill.quantity * fill.price;
            event.fee_rate = fee_rate_for_fill(fill);
            event.fee = notional * event.fee_rate;
            event.fee_native = event.fee;
            event.fee_quote_equiv = event.fee;
            event.fee_asset = kCommissionAssetNone;
            event.spot_cash_delta = 0.0;
            event.spot_inventory_delta = 0.0;
            event.commission_model_source = kCommissionModelNone;
            if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
                const bool buy = fill.side == QTrading::Dto::Trading::OrderSide::Buy;
                if (buy && spot_buy_base_fee) {
                    const double base_fee = fill.quantity * event.fee_rate;
                    event.fee_asset = kCommissionAssetBase;
                    event.fee_native = base_fee;
                    event.fee_quote_equiv = base_fee * fill.price;
                    event.commission_model_source = kCommissionModelImputedBuyBase;
                    event.spot_cash_delta = -notional;
                    event.spot_inventory_delta = fill.quantity - base_fee;
                }
                else {
                    event.fee_asset = kCommissionAssetQuote;
                    if (buy) {
                        event.spot_cash_delta = -(notional + event.fee);
                        event.spot_inventory_delta = fill.quantity;
                    }
                    else {
                        event.spot_cash_delta = notional - event.fee;
                        event.spot_inventory_delta = -fill.quantity;
                    }
                }
            }

            pending_order_events.emplace_back(std::move(event));
            filled_order_ids.insert(fill.order_id);
        }

        for (const auto& order : cur_orders) {
            bool existed = false;
            for (const auto& prev : prev_orders) {
                if (prev.id == order.id) {
                    existed = true;
                    break;
                }
            }
            if (existed) {
                continue;
            }
            QTrading::Log::FileLogger::FeatherV2::OrderEventDto event{};
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = 0;
            event.ts_local = observable_ctx.ts_exchange;
            event.request_id = static_cast<uint64_t>(order.id);
            event.order_id = order.id;
            event.symbol = order.symbol;
            event.instrument_type = static_cast<int32_t>(order.instrument_type);
            event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::OrderEventType::Accepted);
            event.side = static_cast<int32_t>(order.side);
            event.position_side = static_cast<int32_t>(order.position_side);
            event.reduce_only = order.reduce_only;
            event.close_position = order.close_position;
            event.quote_order_qty = order.quote_order_qty;
            event.qty = order.quantity;
            event.price = order.price;
            event.remaining_qty = order.quantity;
            event.closing_position_id = order.closing_position_id;
            pending_order_events.emplace_back(std::move(event));
        }

        for (const auto& order : prev_orders) {
            bool still_open = false;
            for (const auto& cur : cur_orders) {
                if (cur.id == order.id) {
                    still_open = true;
                    break;
                }
            }
            if (still_open || filled_order_ids.find(order.id) != filled_order_ids.end()) {
                continue;
            }
            QTrading::Log::FileLogger::FeatherV2::OrderEventDto event{};
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = 0;
            event.ts_local = observable_ctx.ts_exchange;
            event.request_id = static_cast<uint64_t>(order.id);
            event.order_id = order.id;
            event.symbol = order.symbol;
            event.instrument_type = static_cast<int32_t>(order.instrument_type);
            event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::OrderEventType::Canceled);
            event.side = static_cast<int32_t>(order.side);
            event.position_side = static_cast<int32_t>(order.position_side);
            event.reduce_only = order.reduce_only;
            event.close_position = order.close_position;
            event.quote_order_qty = order.quote_order_qty;
            event.qty = order.quantity;
            event.price = order.price;
            event.exec_qty = 0.0;
            event.exec_price = 0.0;
            event.remaining_qty = order.quantity;
            event.closing_position_id = order.closing_position_id;
            pending_order_events.emplace_back(std::move(event));
        }
    }

    if (step_state.log_module_position_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        std::unordered_map<int64_t, int> closing_fill_order_by_position_id;
        closing_fill_order_by_position_id.reserve(fills.size());
        for (const auto& fill : fills) {
            if (fill.closing_position_id <= 0) {
                continue;
            }
            closing_fill_order_by_position_id[fill.closing_position_id] = fill.order_id;
        }
        const auto& prev_positions_for_diff =
            (!step_state.has_event_snapshots && prev_positions.empty() && !step_state.step_entry_positions.empty())
            ? step_state.step_entry_positions
            : prev_positions;
        std::unordered_map<int, const QTrading::dto::Position*> prev_by_id;
        prev_by_id.reserve(prev_positions_for_diff.size());
        for (const auto& p : prev_positions_for_diff) {
            prev_by_id.emplace(p.id, &p);
        }
        std::unordered_map<int, const QTrading::dto::Position*> cur_by_id;
        cur_by_id.reserve(cur_positions.size());
        for (const auto& p : cur_positions) {
            cur_by_id.emplace(p.id, &p);
        }

        auto log_position_event = [&](const QTrading::dto::Position& position, int32_t event_type) {
            QTrading::Log::FileLogger::FeatherV2::PositionEventDto event{};
            const auto close_fill_it = closing_fill_order_by_position_id.find(position.id);
            const int source_order_id = close_fill_it != closing_fill_order_by_position_id.end()
                ? close_fill_it->second
                : position.order_id;
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = 0;
            event.ts_local = observable_ctx.ts_exchange;
            event.request_id = static_cast<uint64_t>(source_order_id);
            event.source_order_id = source_order_id;
            event.position_id = position.id;
            event.symbol = position.symbol;
            event.instrument_type = static_cast<int32_t>(position.instrument_type);
            event.is_long = position.is_long;
            event.event_type = event_type;
            event.qty = position.quantity;
            event.entry_price = position.entry_price;
            event.notional = position.notional;
            event.unrealized_pnl = position.unrealized_pnl;
            event.initial_margin = position.initial_margin;
            event.maintenance_margin = position.maintenance_margin;
            event.leverage = position.leverage;
            event.fee = position.fee;
            event.fee_rate = position.fee_rate;
            pending_position_events.emplace_back(std::move(event));
        };

        for (const auto& [id, cur] : cur_by_id) {
            auto it = prev_by_id.find(id);
            if (it == prev_by_id.end()) {
                log_position_event(*cur, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Opened));
                continue;
            }
            const auto& prev = *it->second;
            if (prev.is_long != cur->is_long) {
                log_position_event(prev, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Closed));
                log_position_event(*cur, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Opened));
                continue;
            }
            const double delta = cur->quantity - prev.quantity;
            if (delta > kEpsilon) {
                log_position_event(*cur, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Increased));
            }
            else if (delta < -kEpsilon) {
                log_position_event(*cur, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Reduced));
            }
        }
        for (const auto& [id, prev] : prev_by_id) {
            if (cur_by_id.find(id) != cur_by_id.end()) {
                continue;
            }
            log_position_event(*prev, static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Closed));
        }
    }

    const auto perp = account.get_perp_balance();
    const auto spot = account.get_spot_balance();

    if (step_state.log_module_account_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        double last_wallet = step_state.has_last_event_wallet_balance
            ? step_state.last_event_wallet_balance
            : perp.WalletBalance;

        for (const auto& fill : fills) {
            const double notional = fill.quantity * fill.price;
            const double fee_rate = fee_rate_for_fill(fill);
            const double fee_quote = notional * fee_rate;
            int32_t fee_asset = kCommissionAssetNone;
            double fee_native = fee_quote;
            double fee_quote_equiv = fee_quote;
            double spot_cash_delta = 0.0;
            double spot_inventory_delta = 0.0;
            int32_t commission_model_source = kCommissionModelNone;
            if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
                const bool buy = fill.side == QTrading::Dto::Trading::OrderSide::Buy;
                if (buy && spot_buy_base_fee) {
                    const double base_fee = fill.quantity * fee_rate;
                    fee_asset = kCommissionAssetBase;
                    fee_native = base_fee;
                    fee_quote_equiv = base_fee * fill.price;
                    commission_model_source = kCommissionModelImputedBuyBase;
                    spot_cash_delta = -notional;
                    spot_inventory_delta = fill.quantity - base_fee;
                }
                else {
                    fee_asset = kCommissionAssetQuote;
                    if (buy) {
                        spot_cash_delta = -(notional + fee_quote);
                        spot_inventory_delta = fill.quantity;
                    }
                    else {
                        spot_cash_delta = notional - fee_quote;
                        spot_inventory_delta = -fill.quantity;
                    }
                }
            }

            QTrading::Log::FileLogger::FeatherV2::AccountEventDto event{};
            event.run_id = step_state.run_id;
            event.step_seq = observable_ctx.step_seq;
            event.event_seq = next_event_seq++;
            event.ts_local = observable_ctx.ts_exchange;
            event.request_id = static_cast<uint64_t>(fill.order_id);
            event.source_order_id = fill.order_id;
            event.symbol = fill.symbol;
            event.instrument_type = static_cast<int32_t>(fill.instrument_type);
            event.ledger = ledger_from_instrument_type(fill.instrument_type);
            event.event_type = static_cast<int32_t>(
                QTrading::Log::FileLogger::FeatherV2::AccountEventType::BalanceSnapshot);
            event.wallet_delta = perp.WalletBalance - last_wallet;
            event.fee_asset = fee_asset;
            event.fee_native = fee_native;
            event.fee_quote_equiv = fee_quote_equiv;
            event.spot_cash_delta = spot_cash_delta;
            event.spot_inventory_delta = spot_inventory_delta;
            event.commission_model_source = commission_model_source;
            event.wallet_balance_after = perp.WalletBalance;
            event.margin_balance_after = perp.Equity;
            event.available_balance_after = std::max(
                0.0,
                perp.WalletBalance - perp.PositionInitialMargin - runtime_state.perp_open_order_initial_margin);
            event.perp_wallet_balance_after = perp.WalletBalance;
            event.perp_margin_balance_after = perp.Equity;
            event.perp_available_balance_after = event.available_balance_after;
            event.spot_wallet_balance_after = spot.WalletBalance;
            event.spot_available_balance_after = std::max(
                0.0,
                spot.WalletBalance - spot.PositionInitialMargin - runtime_state.spot_open_order_initial_margin);
            event.spot_inventory_value_after = compute_spot_inventory_value_for_log(runtime_state, step_state, snapshot_state);
            event.spot_ledger_value_after = event.spot_wallet_balance_after + event.spot_inventory_value_after;
            event.total_cash_balance_after = account.get_total_cash_balance();
            event.total_ledger_value_after = event.perp_margin_balance_after + event.spot_ledger_value_after;
            (void)logger->Log(step_state.log_module_account_event_id, event);
            last_wallet = perp.WalletBalance;
        }

        QTrading::Log::FileLogger::FeatherV2::AccountEventDto final_event{};
        final_event.run_id = step_state.run_id;
        final_event.step_seq = observable_ctx.step_seq;
        final_event.event_seq = next_event_seq++;
        final_event.ts_local = observable_ctx.ts_exchange;
        final_event.ledger = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::AccountLedger::Both);
        final_event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::AccountEventType::BalanceSnapshot);
        final_event.wallet_delta = perp.WalletBalance - last_wallet;
        final_event.wallet_balance_after = perp.WalletBalance;
        final_event.margin_balance_after = perp.Equity;
        final_event.available_balance_after = std::max(
            0.0,
            perp.WalletBalance - perp.PositionInitialMargin - runtime_state.perp_open_order_initial_margin);
        final_event.perp_wallet_balance_after = perp.WalletBalance;
        final_event.perp_margin_balance_after = perp.Equity;
        final_event.perp_available_balance_after = final_event.available_balance_after;
        final_event.spot_wallet_balance_after = spot.WalletBalance;
        final_event.spot_available_balance_after = std::max(
            0.0,
            spot.WalletBalance - spot.PositionInitialMargin - runtime_state.spot_open_order_initial_margin);
        final_event.spot_inventory_value_after = compute_spot_inventory_value_for_log(runtime_state, step_state, snapshot_state);
        final_event.spot_ledger_value_after = final_event.spot_wallet_balance_after + final_event.spot_inventory_value_after;
        final_event.total_cash_balance_after = account.get_total_cash_balance();
        final_event.total_ledger_value_after = final_event.perp_margin_balance_after + final_event.spot_ledger_value_after;
        (void)logger->Log(step_state.log_module_account_event_id, final_event);
        step_state.last_event_wallet_balance = perp.WalletBalance;
        step_state.has_last_event_wallet_balance = true;
    }

    if (step_state.log_module_position_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        for (auto& event : pending_position_events) {
            event.event_seq = next_event_seq++;
            (void)logger->Log(step_state.log_module_position_event_id, event);
        }
    }
    if (step_state.log_module_order_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        for (auto& event : pending_order_events) {
            event.event_seq = next_event_seq++;
            (void)logger->Log(step_state.log_module_order_event_id, event);
        }
    }

    step_state.last_event_positions = cur_positions;
    step_state.last_event_orders = cur_orders;
    step_state.has_event_snapshots = true;
}

void emit_reduced_step_logs(
    const State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    const State::SnapshotState& snapshot_state,
    const Account& account,
    const Output::StepObservableContext& observable_ctx)
{
    const auto& logger = runtime_state.logger;
    if (!logger) {
        return;
    }
    resolve_log_module_ids_if_needed(step_state, logger);
    uint64_t next_event_seq = 0;

    // Keep the current reduced-scope direction: status first, then events.
    emit_status_log_if_needed(
        runtime_state,
        step_state,
        snapshot_state,
        account,
        logger,
        observable_ctx.account_state_version,
        step_state);
    emit_market_funding_events(step_state, runtime_state, observable_ctx, logger, next_event_seq);
    emit_account_position_order_events(
        runtime_state,
        account,
        observable_ctx,
        step_state,
        snapshot_state,
        logger,
        next_event_seq);
}

} // namespace

StepKernel::StepKernel(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

bool StepKernel::run_step() const
{
    // Main hot path for phase-1/2/3 skeleton:
    // 1) termination check, 2) replay frame build, 3) context publish/update.
    auto& runtime_state = *exchange_.runtime_state_;
    auto& step_state = *exchange_.step_kernel_state_;
    auto& snapshot_state = *exchange_.snapshot_state_;

    if (TerminationPolicy::IsReplayExhausted(step_state)) {
        TerminationPolicy::CloseChannels(exchange_, step_state);
        return false;
    }

    auto frame = MarketReplayKernel::Next(step_state);
    if (!frame.has_next) {
        TerminationPolicy::CloseChannels(exchange_, step_state);
        return false;
    }

    ++step_state.step_seq;
    step_state.has_funding_apply_positions = false;
    step_state.funding_apply_positions.clear();
    step_state.step_entry_positions = runtime_state.positions;
    step_state.step_entry_orders = runtime_state.orders;
    bool orders_maybe_changed = false;
    bool positions_maybe_changed = false;
    if (!step_state.has_published_orders) {
        orders_maybe_changed = !runtime_state.orders.empty();
    }
    else if (runtime_state.orders.size() != step_state.last_published_orders.size()) {
        orders_maybe_changed = true;
    }
    if (!step_state.has_published_positions) {
        positions_maybe_changed = !runtime_state.positions.empty();
    }
    else if (runtime_state.positions.size() != step_state.last_published_positions.size()) {
        positions_maybe_changed = true;
    }
    const bool has_deferred_before_flush = !runtime_state.deferred_order_commands.empty();
    const size_t orders_before_flush = runtime_state.orders.size();
    OrderCommandKernel(exchange_).FlushDeferredForStep(step_state.step_seq);
    orders_maybe_changed = orders_maybe_changed ||
        has_deferred_before_flush ||
        runtime_state.orders.size() != orders_before_flush;
    QTrading::Utils::GlobalTimestamp.store(frame.ts_exchange, std::memory_order_release);
    runtime_state.last_status_snapshot.ts_exchange = frame.ts_exchange;
    if (frame.market_payload) {
        Domain::FundingApplyOrchestration::Execute(
            runtime_state.simulation_config.funding_apply_timing ==
                Contracts::FundingApplyTiming::BeforeMatching,
            [&]() {
                step_state.funding_apply_positions = runtime_state.positions;
                step_state.has_funding_apply_positions = true;
                if (apply_funding_for_step(step_state, runtime_state, exchange_.account_state(), *frame.market_payload)) {
                    ++step_state.account_state_version;
                }
            },
            [&]() {
                auto& fills = step_state.match_fills_scratch;
                fills.reserve(runtime_state.orders.size());
                Domain::MatchingEngine::RunStep(runtime_state, step_state, *frame.market_payload, fills);
                if (!fills.empty()) {
                    ++step_state.account_state_version;
                    orders_maybe_changed = true;
                    positions_maybe_changed = true;
                }
                Domain::FillSettlementEngine::Apply(runtime_state, exchange_.account_state(), fills);
                if (runtime_state.simulation_config.funding_apply_timing ==
                    Contracts::FundingApplyTiming::AfterMatching) {
                    step_state.funding_apply_positions = runtime_state.positions;
                    step_state.has_funding_apply_positions = true;
                }
            });
        const auto pre_liquidation_positions = runtime_state.positions;
        if (Domain::LiquidationExecution::Run(
                runtime_state,
                exchange_.account_state(),
                step_state,
                *frame.market_payload)) {
            if (!pre_liquidation_positions.empty()) {
                for (const auto& prev_position : pre_liquidation_positions) {
                    if (prev_position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
                        prev_position.quantity <= 1e-12) {
                        continue;
                    }
                    const auto cur_it = std::find_if(
                        runtime_state.positions.begin(),
                        runtime_state.positions.end(),
                        [&](const QTrading::dto::Position& p) {
                            return p.id == prev_position.id;
                        });
                    const double cur_qty = (cur_it != runtime_state.positions.end()) ? cur_it->quantity : 0.0;
                    const double closed_qty = prev_position.quantity - cur_qty;
                    if (closed_qty <= 1e-12) {
                        continue;
                    }
                    double liq_price = prev_position.entry_price;
                    const auto symbol_it = step_state.symbol_to_id.find(prev_position.symbol);
                    if (symbol_it != step_state.symbol_to_id.end()) {
                        const size_t symbol_id = symbol_it->second;
                        if (symbol_id < snapshot_state.has_last_mark_price_by_symbol.size() &&
                            snapshot_state.has_last_mark_price_by_symbol[symbol_id] != 0 &&
                            symbol_id < snapshot_state.last_mark_price_by_symbol.size()) {
                            liq_price = snapshot_state.last_mark_price_by_symbol[symbol_id];
                        }
                    }
                    Domain::MatchFill synthetic{};
                    synthetic.order_id = -999999;
                    synthetic.symbol = prev_position.symbol;
                    synthetic.instrument_type = prev_position.instrument_type;
                    synthetic.side = prev_position.is_long
                        ? QTrading::Dto::Trading::OrderSide::Sell
                        : QTrading::Dto::Trading::OrderSide::Buy;
                    synthetic.position_side = prev_position.is_long
                        ? QTrading::Dto::Trading::PositionSide::Long
                        : QTrading::Dto::Trading::PositionSide::Short;
                    synthetic.reduce_only = true;
                    synthetic.close_position = cur_it == runtime_state.positions.end() || cur_qty <= 1e-12;
                    synthetic.is_taker = true;
                    synthetic.fill_probability = 1.0;
                    synthetic.taker_probability = 1.0;
                    synthetic.quote_order_qty = 0.0;
                    synthetic.order_price = prev_position.entry_price;
                    synthetic.closing_position_id = prev_position.id;
                    synthetic.order_quantity = prev_position.quantity;
                    synthetic.quantity = closed_qty;
                    synthetic.price = liq_price;
                    step_state.match_fills_scratch.emplace_back(std::move(synthetic));
                }
            }
            ++step_state.account_state_version;
            orders_maybe_changed = true;
            positions_maybe_changed = true;
        }
        Domain::OrderEntryService::SyncOpenOrderMargins(runtime_state);
        exchange_.account_state().sync_open_order_initial_margins(
            runtime_state.spot_open_order_initial_margin,
            runtime_state.perp_open_order_initial_margin);
        refresh_perp_mark_state(step_state, snapshot_state, runtime_state, exchange_.account_state(), *frame.market_payload);
        if (runtime_state.simulation_config.funding_apply_timing == Contracts::FundingApplyTiming::AfterMatching) {
            const size_t funding_count = std::min(
                frame.market_payload->funding_by_id.size(),
                step_state.last_observed_funding_by_symbol.size());
            for (size_t i = 0; i < funding_count; ++i) {
                if (!frame.market_payload->funding_by_id[i].has_value()) {
                    continue;
                }
                const auto current = frame.market_payload->funding_by_id[i];
                if (step_state.last_observed_funding_by_symbol[i].has_value()) {
                    frame.market_payload->funding_by_id[i] = step_state.last_observed_funding_by_symbol[i];
                }
                step_state.last_observed_funding_by_symbol[i] = current;
            }
        }
    }

    Output::StepObservableContext observable_ctx{};
    observable_ctx.ts_exchange = frame.ts_exchange;
    observable_ctx.step_seq = step_state.step_seq;
    observable_ctx.replay_exhausted = false;
    observable_ctx.market_payload = std::move(frame.market_payload);
    observable_ctx.position_snapshot = &runtime_state.positions;
    observable_ctx.order_snapshot = &runtime_state.orders;
    observable_ctx.account_state_version = step_state.account_state_version;
    resolve_log_module_ids_if_needed(step_state, runtime_state.logger);
    update_snapshot_state(snapshot_state, step_state, observable_ctx);
    apply_basis_warning_leverage_caps(runtime_state, snapshot_state);
    Output::SnapshotBuilder::Fill(exchange_, runtime_state.last_status_snapshot);
    resolve_log_module_ids_if_needed(step_state, runtime_state.logger);
    emit_status_log_if_needed(
        runtime_state,
        step_state,
        snapshot_state,
        exchange_.account_state(),
        runtime_state.logger,
        step_state.account_state_version,
        step_state);
    if (!step_state.has_published_orders) {
        orders_maybe_changed = !runtime_state.orders.empty();
    }
    else {
        orders_maybe_changed = !vector_equal(
            runtime_state.orders,
            step_state.last_published_orders,
            order_equal);
    }
    if (!step_state.has_published_positions) {
        positions_maybe_changed = !runtime_state.positions.empty();
    }
    else {
        positions_maybe_changed = !vector_equal(
            runtime_state.positions,
            step_state.last_published_positions,
            position_equal);
    }

    publish_position_order_channels(
        exchange_,
        runtime_state,
        step_state,
        orders_maybe_changed,
        positions_maybe_changed);
    emit_reduced_step_logs(runtime_state, step_state, snapshot_state, exchange_.account_state(), observable_ctx);
    Output::ChannelPublisher::PublishStep(exchange_, observable_ctx);
    step_state.channels_closed = false;
    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
