#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Dto/AccountLog.hpp"
#include "Exchanges/BinanceSimulator/Application/MarketReplayKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"
#include "Exchanges/BinanceSimulator/Domain/FundingApplyOrchestration.hpp"
#include "Exchanges/BinanceSimulator/Domain/FundingEligibilityDecision.hpp"
#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"
#include "Exchanges/BinanceSimulator/Output/ChannelPublisher.hpp"
#include "Exchanges/BinanceSimulator/Output/StepObservableContext.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/SnapshotState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"
#include "FileLogger/FeatherV2/FundingEvent.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"
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
        const bool is_duplicate = step_state.last_applied_funding_time_by_symbol[i] == funding.FundingTime;
        const auto action = Domain::FundingEligibilityDecision::Decide(
            is_duplicate,
            funding.MarkPrice.has_value(),
            true);
        if (action == Domain::FundingDecisionAction::SkipNoMark) {
            ++step_state.funding_skipped_no_mark_total;
            continue;
        }
        if (action == Domain::FundingDecisionAction::SkipDuplicate ||
            action == Domain::FundingDecisionAction::NoOp) {
            continue;
        }

        const std::string& symbol = step_state.symbols[i];
        const double mark = *funding.MarkPrice;
        for (const auto& position : runtime_state.positions) {
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

void publish_position_order_channels(
    BinanceExchange& exchange,
    const State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state)
{
    if (step_state.account_state_version == step_state.last_published_account_state_version) {
        return;
    }

    const auto& orders = runtime_state.orders;
    if (!step_state.has_published_orders) {
        if (!orders.empty() && exchange.get_order_channel()) {
            exchange.get_order_channel()->Send(orders);
            step_state.last_published_orders = orders;
            step_state.has_published_orders = true;
        }
    }
    else if (!vector_equal(orders, step_state.last_published_orders, order_equal)) {
        if (exchange.get_order_channel()) {
            exchange.get_order_channel()->Send(orders);
        }
        step_state.last_published_orders = orders;
    }

    const auto& positions = runtime_state.positions;
    if (!step_state.has_published_positions) {
        if (!positions.empty() && exchange.get_position_channel()) {
            exchange.get_position_channel()->Send(positions);
            step_state.last_published_positions = positions;
            step_state.has_published_positions = true;
        }
    }
    else if (!vector_equal(positions, step_state.last_published_positions, position_equal)) {
        if (exchange.get_position_channel()) {
            exchange.get_position_channel()->Send(positions);
        }
        step_state.last_published_positions = positions;
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
    step_state.log_module_market_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
    step_state.log_module_funding_event_id = logger->GetModuleId(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::FundingEvent));
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
    row.perp_available_balance = perp.AvailableBalance;
    row.perp_ledger_value = perp.Equity;
    row.spot_cash_balance = spot.WalletBalance;
    row.spot_available_balance = spot.AvailableBalance;
    row.spot_inventory_value = spot_inventory_value;
    row.spot_ledger_value = spot_ledger_value;
    row.total_cash_balance = total_cash;
    row.total_ledger_value = perp.Equity + spot_ledger_value;
    (void)logger->Log(mutable_step_state.log_module_account_id, row);
    mutable_step_state.last_logged_status_version = account_state_version;
}

void emit_market_funding_events(
    const State::StepKernelState& step_state,
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Output::StepObservableContext& observable_ctx,
    const std::shared_ptr<QTrading::Log::Logger>& logger)
{
    if (!logger || !observable_ctx.market_payload) {
        return;
    }

    Logging::StepLogContext ctx{};
    ctx.run_id = step_state.run_id;
    ctx.step_seq = observable_ctx.step_seq;
    ctx.ts_exchange = observable_ctx.ts_exchange;
    ctx.ts_local = observable_ctx.ts_exchange;

    const auto& payload = *observable_ctx.market_payload;
    if (payload.symbols &&
        step_state.log_module_market_event_id != QTrading::Log::Logger::kInvalidModuleId) {
        const size_t count = payload.symbols->size();
        for (size_t i = 0; i < count; ++i) {
            QTrading::Log::FileLogger::FeatherV2::MarketEventDto event{};
            event.run_id = ctx.run_id;
            event.step_seq = ctx.step_seq;
            event.event_seq = ctx.next_event_seq();
            event.ts_local = ctx.ts_local;
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
            (void)logger->Log(step_state.log_module_market_event_id, event);
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
        if (!funding.MarkPrice.has_value()) {
            QTrading::Log::FileLogger::FeatherV2::FundingEventDto skipped{};
            skipped.run_id = ctx.run_id;
            skipped.step_seq = ctx.step_seq;
            skipped.event_seq = ctx.next_event_seq();
            skipped.ts_local = ctx.ts_local;
            skipped.symbol = symbol;
            skipped.instrument_type = static_cast<int32_t>(QTrading::Dto::Trading::InstrumentType::Perp);
            skipped.funding_time = funding.FundingTime;
            skipped.rate = funding.Rate;
            skipped.has_mark_price = false;
            skipped.mark_price_source = 0;
            skipped.skip_reason = 1;
            skipped.position_id = -1;
            (void)logger->Log(step_state.log_module_funding_event_id, skipped);
            continue;
        }

        const double mark = *funding.MarkPrice;
        for (const auto& position : runtime_state.positions) {
            if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
                position.symbol != symbol ||
                position.quantity <= kEpsilon) {
                continue;
            }
            QTrading::Log::FileLogger::FeatherV2::FundingEventDto applied{};
            applied.run_id = ctx.run_id;
            applied.step_seq = ctx.step_seq;
            applied.event_seq = ctx.next_event_seq();
            applied.ts_local = ctx.ts_local;
            applied.symbol = symbol;
            applied.instrument_type = static_cast<int32_t>(QTrading::Dto::Trading::InstrumentType::Perp);
            applied.funding_time = funding.FundingTime;
            applied.rate = funding.Rate;
            applied.has_mark_price = true;
            applied.mark_price = mark;
            applied.mark_price_source = 1;
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

    // Keep baseline direction in reduced scope: status first, then events.
    emit_status_log_if_needed(
        runtime_state,
        step_state,
        snapshot_state,
        account,
        logger,
        observable_ctx.account_state_version,
        step_state);
    emit_market_funding_events(step_state, runtime_state, observable_ctx, logger);
    step_state.last_logged_event_version = observable_ctx.account_state_version;
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
    OrderCommandKernel(exchange_).FlushDeferredForStep(step_state.step_seq);
    QTrading::Utils::GlobalTimestamp.store(frame.ts_exchange, std::memory_order_release);
    runtime_state.last_status_snapshot.ts_exchange = frame.ts_exchange;
    if (frame.market_payload) {
        Domain::FundingApplyOrchestration::Execute(
            runtime_state.simulation_config.funding_apply_timing ==
                Contracts::FundingApplyTiming::BeforeMatching,
            [&]() {
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
                }
                Domain::FillSettlementEngine::Apply(runtime_state, exchange_.account_state(), fills);
            });
    }

    Output::StepObservableContext observable_ctx{};
    observable_ctx.ts_exchange = frame.ts_exchange;
    observable_ctx.step_seq = step_state.step_seq;
    observable_ctx.replay_exhausted = false;
    observable_ctx.market_payload = std::move(frame.market_payload);
    observable_ctx.position_snapshot = &runtime_state.positions;
    observable_ctx.order_snapshot = &runtime_state.orders;
    observable_ctx.account_state_version = step_state.account_state_version;
    update_snapshot_state(snapshot_state, step_state, observable_ctx);
    emit_reduced_step_logs(runtime_state, step_state, snapshot_state, exchange_.account_state(), observable_ctx);
    Output::ChannelPublisher::PublishStep(exchange_, observable_ctx);
    publish_position_order_channels(exchange_, runtime_state, step_state);
    step_state.channels_closed = false;
    return true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
