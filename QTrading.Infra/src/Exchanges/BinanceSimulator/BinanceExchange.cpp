#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"
#include "Diagnostics/Trace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <memory_resource>
#include <unordered_set>
#include <utility>

using namespace QTrading;
using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
using namespace QTrading::Log;
using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
using QTrading::Log::FileLogger::FeatherV2::AccountEventType;
using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;
using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
using QTrading::Log::FileLogger::FeatherV2::OrderEventType;
using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

namespace {

std::pmr::synchronized_pool_resource& log_pool()
{
    static std::pmr::synchronized_pool_resource pool{ std::pmr::new_delete_resource() };
    return pool;
}

template <typename T, typename... Args>
std::shared_ptr<T> make_pooled(Args&&... args)
{
    auto& pool = log_pool();
    std::pmr::polymorphic_allocator<T> alloc(&pool);
    return std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
QTrading::Log::PayloadPtr make_log_payload(Args&&... args)
{
    return QTrading::Log::MakePayload<T>(&log_pool(), std::forward<Args>(args)...);
}

template <typename T>
void LogBatchPooled(QTrading::Log::Logger* logger,
    QTrading::Log::Logger::ModuleId module_id,
    const std::vector<T>& items)
{
    if (!logger || module_id == QTrading::Log::Logger::kInvalidModuleId || items.empty()) {
        return;
    }
    const auto ts = QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed);
    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.reserve(items.size());
    for (const auto& item : items) {
        payloads.emplace_back(make_log_payload<T>(item));
    }
    logger->LogBatchAt(module_id, payloads.data(), payloads.size(), ts);
}

template <typename T>
void LogBatchPooled(QTrading::Log::Logger* logger,
    QTrading::Log::Logger::ModuleId module_id,
    std::vector<T>& items)
{
    if (!logger || module_id == QTrading::Log::Logger::kInvalidModuleId || items.empty()) {
        return;
    }
    const auto ts = QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed);
    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.reserve(items.size());
    for (auto& item : items) {
        payloads.emplace_back(make_log_payload<T>(std::move(item)));
    }
    logger->LogBatchAt(module_id, payloads.data(), payloads.size(), ts);
}

uint64_t now_ms()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

/// @brief Simulator for Binance futures exchange.
/// @details Reads multiple CSV files (one per symbol), publishes multi-symbol 1-minute bars,
///          updates positions/orders only when they change, and maintains a global timestamp.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level, uint64_t run_id)
    : BinanceExchange(symbolCsv, logger, std::make_shared<Account>(init_balance, vip_level), run_id) { }

/// @brief Primary constructor with an external Account instance.
/// @param symbolCsv  Vector of (symbol, csv_file) pairs to drive market data.
/// @param logger     Shared Logger for writing account/position/order logs.
/// @param account    Shared Account simulation object.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account,
    uint64_t run_id)
    : logger(logger), account(account)
{
    if (logger) {
        account_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Account));
        position_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Position));
        order_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::Order));
        account_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::AccountEvent));
        position_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::PositionEvent));
        order_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::OrderEvent));
        market_event_module_id_ = logger->GetModuleId(LogModuleToString(LogModule::MarketEvent));
    }
    if (run_id == 0) {
        run_id = now_ms();
    }
    log_ctx_.run_id = run_id;
    log_ctx_.step_seq = 0;
    log_ctx_.ts_exchange = 0;
    log_ctx_.event_seq = 0;

    // Load each CSV in parallel (safe: each MarketData instance is independent).
    std::vector<std::future<MarketData>> jobs;
    symbols_.reserve(symbolCsv.size());
    md_.reserve(symbolCsv.size());
    cursor_.assign(symbolCsv.size(), 0);
    next_ts_by_symbol_.assign(symbolCsv.size(), 0);
    has_next_ts_.assign(symbolCsv.size(), 0);
    kline_counts_.reserve(symbolCsv.size());
    jobs.reserve(symbolCsv.size());

    for (const auto& [sym, csv] : symbolCsv) {
        symbols_.push_back(sym);
        jobs.emplace_back(std::async(std::launch::async, [sym, csv]() {
            return MarketData(sym, csv);
            }));
    }

    // Collect results deterministically by input order and initialize heap state.
    for (size_t i = 0; i < jobs.size(); ++i) {
        md_.push_back(jobs[i].get());

        const auto& data = md_[i];
        kline_counts_.push_back(data.get_klines_count());
        if (cursor_[i] < data.get_klines_count()) {
            const uint64_t ts = data.get_kline(cursor_[i]).Timestamp;
            next_ts_by_symbol_[i] = ts;
            has_next_ts_[i] = 1;
            next_ts_heap_.push(HeapItem{ ts, i });
        }
    }
    last_close_by_symbol_.assign(symbols_.size(), 0.0);
    has_last_close_.assign(symbols_.size(), 0);

    /// @details Create bounded channels:
    ///          - market_channel: 8-element sliding window of MultiKlineDto
    ///          - position_channel / order_channel: debounce buffers (latest snapshot is sufficient)
    market_channel = ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(8, OverflowPolicy::DropOldest);
    position_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Position>>();
    order_channel = ChannelFactory::CreateUnboundedChannel<std::vector<dto::Order>>();
}

bool BinanceExchange::place_order(const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->place_order(symbol, quantity, price, side, position_side, reduce_only);
}

bool BinanceExchange::place_order(const std::string& symbol,
    double quantity,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->place_order(symbol, quantity, side, position_side, reduce_only);
}

void BinanceExchange::close_position(const std::string& symbol, double price)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol, price);
}

void BinanceExchange::close_position(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol);
}

void BinanceExchange::close_position(const std::string& symbol,
    QTrading::Dto::Trading::PositionSide position_side,
    double price)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->close_position(symbol, position_side, price);
}

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    QTR_TRACE("ex", "step begin");
    std::lock_guard<std::mutex> lk(account_mtx_);

    // Terminate simulation (end backtest) once wallet balance is depleted.
    if (account->get_balance().WalletBalance <= 0.0) {
        QTR_TRACE("ex", "balance depleted -> close channels");
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    uint64_t ts;
    /// @details Find the next minimum timestamp among all symbols.
    if (!next_timestamp(ts)) {
        QTR_TRACE("ex", "no next timestamp -> close channels");
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    /// @details Publish multi-symbol market data at timestamp `ts`.
    last_step_ts_ = ts;
    log_ctx_.ts_exchange = ts;
    log_ctx_.step_seq += 1;
    log_ctx_.event_seq = 0;
    set_global_timestamp(ts);
    auto dto = make_pooled<MultiKlineDto>();
    build_multikline(ts, *dto);

    QTR_TRACE("ex", "market_channel Send begin");
    market_channel->Send(dto);
    QTR_TRACE("ex", "market_channel Send end");

    log_status();
    log_ctx_.ts_local = now_ms();

    const uint64_t cur_ver = account->get_state_version();
    const auto& curP = account->get_all_positions();
    const auto& curO = account->get_all_open_orders();

    bool pos_changed = false;
    bool ord_changed = false;

    // Only consider sending snapshots when account reported a state transition.
    if (cur_ver != last_account_version_) {
        if (!vec_equal(curP, last_pos_snapshot)) {
            QTR_TRACE("ex", "position_channel Send");
            position_channel->Send(curP);
            pos_changed = true;
        }

        if (!vec_equal(curO, last_ord_snapshot)) {
            QTR_TRACE("ex", "order_channel Send");
            order_channel->Send(curO);
            ord_changed = true;
        }
    }

    log_events(*dto, curP, curO);

    if (cur_ver != last_account_version_) {
        if (pos_changed) {
            last_pos_snapshot = curP;
        }
        if (ord_changed) {
            last_ord_snapshot = curO;
        }
        last_account_version_ = cur_ver;
    }

    QTR_TRACE("ex", "step end");
    return true;
}

/// @brief Get a snapshot of all current positions.
/// @return Const reference to the vector of Position DTOs.
const std::vector<dto::Position>& BinanceExchange::get_all_positions() const {
    return account->get_all_positions();
}

/// @brief Get a snapshot of all current open orders.
/// @return Const reference to the vector of Order DTOs.
const std::vector<dto::Order>& BinanceExchange::get_all_open_orders() const {
    return account->get_all_open_orders();
}

void BinanceExchange::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->set_symbol_leverage(symbol, new_leverage);
}

double BinanceExchange::get_symbol_leverage(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    return account->get_symbol_leverage(symbol);
}

/// @brief Close the simulator: drain CSVs and close all channels.
/// @details Advances each symbol's cursor to the end before closing.
void BinanceExchange::close()
{
    for (size_t i = 0; i < md_.size(); ++i) {
        cursor_[i] = md_[i].get_klines_count();
        has_next_ts_[i] = 0;
    }

	IExchange<MultiKlinePtr>::close();
}

void BinanceExchange::cancel_open_orders(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    account->cancel_open_orders(symbol);
}

/// @brief Determine the next global timestamp to emit.
/// @param[out] ts  The minimum upcoming timestamp among all symbols.
/// @return true if at least one symbol has data remaining.
bool BinanceExchange::next_timestamp(uint64_t& ts)
{
    // Pop stale heap entries until top matches current per-symbol next_ts.
    while (!next_ts_heap_.empty()) {
        const auto top = next_ts_heap_.top();
        if (top.sym_id < next_ts_by_symbol_.size() &&
            has_next_ts_[top.sym_id] &&
            next_ts_by_symbol_[top.sym_id] == top.ts) {
            ts = top.ts;
            return true;
        }
        next_ts_heap_.pop();
    }
    ts = std::numeric_limits<uint64_t>::max();
    return false;
}

/// @brief Build and send a MultiKlineDto for timestamp `ts`.
/// @param ts   Global timestamp to align on.
/// @param out  DTO to populate with per-symbol optional KlineDto.
void BinanceExchange::build_multikline(uint64_t ts, MultiKlineDto& out)
{
    out.Timestamp = ts;
    out.klines.clear();
    out.klines.reserve(md_.size());

    kline_snap_cache_.clear();
    kline_snap_cache_.reserve(md_.size());

    for (size_t i = 0; i < md_.size(); ++i) {
        const auto& sym = symbols_[i];
        auto& data = md_[i];
        size_t idx = cursor_[i];
        if (idx < data.get_klines_count() &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            out.klines.emplace(sym, k);
            kline_snap_cache_.emplace(sym, k);
            last_close_by_symbol_[i] = k.ClosePrice;
            has_last_close_[i] = 1;
            ++cursor_[i];

            // Advance this symbol in the multiway merge heap.
            if (cursor_[i] < data.get_klines_count()) {
                const uint64_t next_ts = data.get_kline(cursor_[i]).Timestamp;
                next_ts_by_symbol_[i] = next_ts;
                has_next_ts_[i] = 1;
                next_ts_heap_.push(HeapItem{ next_ts, i });
            }
            else {
                has_next_ts_[i] = 0;
            }
        }
        else {
            out.klines.emplace(sym, std::nullopt);
        }
    }
    if (!kline_snap_cache_.empty())
        account->update_positions(kline_snap_cache_);
}

/// @brief Log account balance, positions, and orders via the Logger.
/// @details Uses Arrow Feather-V2 for efficient batch logging.
void BinanceExchange::log_status() {
    if (!logger) {
        return;
    }

    const uint64_t cur_ver = account->get_state_version();
    if (cur_ver == last_logged_version_) {
        return;
    }

    if (account_module_id_ != Logger::kInvalidModuleId) {
        auto snap = account->get_balance();
        logger->Log(account_module_id_, make_log_payload<AccountLog>(
            AccountLog{ snap.WalletBalance, snap.UnrealizedPnl, snap.MarginBalance }));
    }
    if (position_module_id_ != Logger::kInvalidModuleId) {
        const auto& curP = account->get_all_positions();
        LogBatchPooled(logger.get(), position_module_id_, curP);
    }
    if (order_module_id_ != Logger::kInvalidModuleId) {
        const auto& curO = account->get_all_open_orders();
        LogBatchPooled(logger.get(), order_module_id_, curO);
    }

    last_logged_version_ = cur_ver;
}

void BinanceExchange::log_events(const MultiKlineDto& market,
    const std::vector<dto::Position>& cur_positions,
    const std::vector<dto::Order>& cur_orders)
{
    if (!logger) {
        return;
    }

    account_event_buffer_.reset(log_ctx_);
    position_event_buffer_.reset(log_ctx_);
    order_event_buffer_.reset(log_ctx_);
    market_event_buffer_.reset(log_ctx_);

    if (market_event_module_id_ != Logger::kInvalidModuleId) {
        market_event_buffer_.reserve(symbols_.size());
        for (const auto& sym : symbols_) {
            MarketEventDto e;
            e.symbol = sym;
            auto it = market.klines.find(sym);
            if (it != market.klines.end() && it->second.has_value()) {
                const auto& k = it->second.value();
                e.has_kline = true;
                e.open = k.OpenPrice;
                e.high = k.HighPrice;
                e.low = k.LowPrice;
                e.close = k.ClosePrice;
                e.volume = k.Volume;
                e.taker_buy_base_volume = k.TakerBuyBaseVolume;
            }
            else {
                e.has_kline = false;
            }
            market_event_buffer_.push(std::move(e));
        }

        LogBatchPooled(logger.get(), market_event_module_id_, market_event_buffer_.events);
    }

    const uint64_t cur_ver = account->get_state_version();
    if (cur_ver == last_event_version_) {
        return;
    }

    const auto balance = account->get_balance();
    const auto fill_events = account->drain_fill_events();
    std::unordered_set<int> filled_order_ids;
    if (!fill_events.empty()) {
        filled_order_ids.reserve(fill_events.size() * 2);
    }

    if (account_event_module_id_ != Logger::kInvalidModuleId) {
        double last_wallet_for_fill = last_wallet_balance_.value_or(
            fill_events.empty() ? balance.WalletBalance : fill_events.front().balance_snapshot.WalletBalance);

        for (const auto& f : fill_events) {
            AccountEventDto e;
            e.request_id = static_cast<uint64_t>(f.order_id);
            e.source_order_id = f.order_id;
            e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
            e.wallet_delta = f.balance_snapshot.WalletBalance - last_wallet_for_fill;
            e.wallet_balance_after = f.balance_snapshot.WalletBalance;
            e.margin_balance_after = f.balance_snapshot.MarginBalance;
            e.available_balance_after = f.balance_snapshot.AvailableBalance;
            account_event_buffer_.push(std::move(e));
            last_wallet_for_fill = f.balance_snapshot.WalletBalance;
        }

        AccountEventDto e;
        e.request_id = 0;
        e.source_order_id = -1;
        e.event_type = static_cast<int32_t>(AccountEventType::BalanceSnapshot);
        e.wallet_delta = balance.WalletBalance - last_wallet_for_fill;
        e.wallet_balance_after = balance.WalletBalance;
        e.margin_balance_after = balance.MarginBalance;
        e.available_balance_after = balance.AvailableBalance;
        account_event_buffer_.push(std::move(e));

        LogBatchPooled(logger.get(), account_event_module_id_, account_event_buffer_.events);
    }
    last_wallet_balance_ = balance.WalletBalance;

    const bool need_position_events = position_event_module_id_ != Logger::kInvalidModuleId;
    const bool need_order_events = order_event_module_id_ != Logger::kInvalidModuleId;
    if (need_position_events || need_order_events) {
        std::unordered_map<int, const dto::Position*> prev_pos_by_id;
        prev_pos_by_id.reserve(last_pos_snapshot.size());
        for (const auto& p : last_pos_snapshot) {
            prev_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_map<int, const dto::Position*> cur_pos_by_id;
        cur_pos_by_id.reserve(cur_positions.size());
        for (const auto& p : cur_positions) {
            cur_pos_by_id.emplace(p.id, &p);
        }

        std::unordered_set<int> changed_position_ids;
        changed_position_ids.reserve(cur_positions.size() + last_pos_snapshot.size());

        auto push_position_event = [&](const dto::Position& p, PositionEventType type) {
            PositionEventDto e;
            e.request_id = static_cast<uint64_t>(p.order_id);
            e.source_order_id = p.order_id;
            e.position_id = p.id;
            e.symbol = p.symbol;
            e.is_long = p.is_long;
            e.event_type = static_cast<int32_t>(type);
            e.qty = p.quantity;
            e.entry_price = p.entry_price;
            e.notional = p.notional;
            e.unrealized_pnl = p.unrealized_pnl;
            e.initial_margin = p.initial_margin;
            e.maintenance_margin = p.maintenance_margin;
            e.leverage = p.leverage;
            e.fee = p.fee;
            e.fee_rate = p.fee_rate;
            position_event_buffer_.push(std::move(e));
        };

        constexpr double kQtyEps = 1e-8;
        if (need_position_events) {
            for (const auto& f : fill_events) {
                for (const auto& p : f.positions_snapshot) {
                    PositionEventDto e;
                    e.request_id = static_cast<uint64_t>(f.order_id);
                    e.source_order_id = f.order_id;
                    e.position_id = p.id;
                    e.symbol = p.symbol;
                    e.is_long = p.is_long;
                    e.event_type = static_cast<int32_t>(PositionEventType::Snapshot);
                    e.qty = p.quantity;
                    e.entry_price = p.entry_price;
                    e.notional = p.notional;
                    e.unrealized_pnl = p.unrealized_pnl;
                    e.initial_margin = p.initial_margin;
                    e.maintenance_margin = p.maintenance_margin;
                    e.leverage = p.leverage;
                    e.fee = p.fee;
                    e.fee_rate = p.fee_rate;
                    position_event_buffer_.push(std::move(e));
                }
            }

            for (const auto& kv : cur_pos_by_id) {
                const auto& cur = *kv.second;
                auto it = prev_pos_by_id.find(kv.first);
                if (it == prev_pos_by_id.end()) {
                    push_position_event(cur, PositionEventType::Opened);
                    changed_position_ids.insert(cur.id);
                    continue;
                }
                const auto& prev = *it->second;
                const double delta_qty = cur.quantity - prev.quantity;
                if (std::abs(delta_qty) > kQtyEps) {
                    auto type = delta_qty > 0.0 ? PositionEventType::Increased : PositionEventType::Reduced;
                    push_position_event(cur, type);
                    changed_position_ids.insert(cur.id);
                }
            }

            for (const auto& kv : prev_pos_by_id) {
                if (cur_pos_by_id.find(kv.first) == cur_pos_by_id.end()) {
                    push_position_event(*kv.second, PositionEventType::Closed);
                    changed_position_ids.insert(kv.first);
                }
            }

            LogBatchPooled(logger.get(), position_event_module_id_, position_event_buffer_.events);
        }
        else {
            for (const auto& kv : cur_pos_by_id) {
                auto it = prev_pos_by_id.find(kv.first);
                if (it == prev_pos_by_id.end()) {
                    changed_position_ids.insert(kv.first);
                    continue;
                }
                const auto& cur = *kv.second;
                const auto& prev = *it->second;
                if (std::abs(cur.quantity - prev.quantity) > kQtyEps) {
                    changed_position_ids.insert(kv.first);
                }
            }
            for (const auto& kv : prev_pos_by_id) {
                if (cur_pos_by_id.find(kv.first) == cur_pos_by_id.end()) {
                    changed_position_ids.insert(kv.first);
                }
            }
        }

        if (need_order_events) {
            std::unordered_map<int, const dto::Order*> prev_ord_by_id;
            prev_ord_by_id.reserve(last_ord_snapshot.size());
            for (const auto& o : last_ord_snapshot) {
                prev_ord_by_id.emplace(o.id, &o);
            }

            std::unordered_map<int, const dto::Order*> cur_ord_by_id;
            cur_ord_by_id.reserve(cur_orders.size());
            for (const auto& o : cur_orders) {
                cur_ord_by_id.emplace(o.id, &o);
            }

            auto push_order_event = [&](const dto::Order& o, OrderEventType type,
                double exec_qty, double exec_price, double remaining_qty) {
                OrderEventDto e;
                e.request_id = static_cast<uint64_t>(o.id);
                e.order_id = o.id;
                e.symbol = o.symbol;
                e.event_type = static_cast<int32_t>(type);
                e.side = static_cast<int32_t>(o.side);
                e.position_side = static_cast<int32_t>(o.position_side);
                e.reduce_only = o.reduce_only;
                e.qty = o.quantity;
                e.price = o.price;
                e.exec_qty = exec_qty;
                e.exec_price = exec_price;
                e.remaining_qty = remaining_qty;
                e.closing_position_id = o.closing_position_id;
                e.is_taker = false;
                e.fee = 0.0;
                e.fee_rate = 0.0;
                e.reject_reason = 0;
                order_event_buffer_.push(std::move(e));
            };

            for (const auto& f : fill_events) {
                OrderEventDto e;
                e.request_id = static_cast<uint64_t>(f.order_id);
                e.order_id = f.order_id;
                e.symbol = f.symbol;
                e.event_type = static_cast<int32_t>(OrderEventType::Filled);
                e.side = static_cast<int32_t>(f.side);
                e.position_side = static_cast<int32_t>(f.position_side);
                e.reduce_only = f.reduce_only;
                e.qty = f.order_qty;
                e.price = f.order_price;
                e.exec_qty = f.exec_qty;
                e.exec_price = f.exec_price;
                e.remaining_qty = f.remaining_qty;
                e.closing_position_id = f.closing_position_id;
                e.is_taker = f.is_taker;
                e.fee = f.fee;
                e.fee_rate = f.fee_rate;
                e.reject_reason = 0;
                order_event_buffer_.push(std::move(e));
                filled_order_ids.insert(f.order_id);
            }

            for (const auto& kv : cur_ord_by_id) {
                if (prev_ord_by_id.find(kv.first) == prev_ord_by_id.end()) {
                    const auto& o = *kv.second;
                    push_order_event(o, OrderEventType::Accepted, 0.0, 0.0, o.quantity);
                }
            }

            for (const auto& kv : prev_ord_by_id) {
                if (cur_ord_by_id.find(kv.first) != cur_ord_by_id.end()) {
                    continue;
                }
                const auto& o = *kv.second;
                if (filled_order_ids.find(o.id) != filled_order_ids.end()) {
                    continue;
                }
                push_order_event(o, OrderEventType::Canceled, 0.0, 0.0, o.quantity);
            }

            LogBatchPooled(logger.get(), order_event_module_id_, order_event_buffer_.events);
        }
    }

    last_event_version_ = cur_ver;
}

void BinanceExchange::FillStatusSnapshot(StatusSnapshot& out) const
{
    std::lock_guard<std::mutex> lk(account_mtx_);
    out.ts_exchange = last_step_ts_;
    if (account) {
        const auto bal = account->get_balance();
        out.wallet_balance = bal.WalletBalance;
        out.margin_balance = bal.MarginBalance;
        out.available_balance = bal.AvailableBalance;
        out.unrealized_pnl = bal.UnrealizedPnl;
        out.total_unrealized_pnl = account->total_unrealized_pnl();
    }
    else {
        out.wallet_balance = 0.0;
        out.margin_balance = 0.0;
        out.available_balance = 0.0;
        out.unrealized_pnl = 0.0;
        out.total_unrealized_pnl = 0.0;
    }
    out.progress_pct = progress_pct_();
    out.prices.clear();
    out.prices.reserve(symbols_.size());
    for (size_t i = 0; i < symbols_.size(); ++i) {
        StatusSnapshot::PriceSnapshot snap;
        snap.symbol = symbols_[i];
        snap.price = last_close_by_symbol_[i];
        snap.has_price = has_last_close_[i] != 0;
        out.prices.emplace_back(std::move(snap));
    }
}

double BinanceExchange::progress_pct_() const
{
    if (kline_counts_.empty()) {
        return 0.0;
    }
    double min_ratio = 1.0;
    bool has_count = false;
    const size_t count = std::min(kline_counts_.size(), cursor_.size());
    for (size_t i = 0; i < count; ++i) {
        const auto total = kline_counts_[i];
        if (total == 0) {
            continue;
        }
        has_count = true;
        double ratio = static_cast<double>(cursor_[i]) / static_cast<double>(total);
        if (ratio < min_ratio) {
            min_ratio = ratio;
        }
    }
    if (!has_count) {
        return 0.0;
    }
    if (min_ratio < 0.0) {
        min_ratio = 0.0;
    }
    if (min_ratio > 1.0) {
        min_ratio = 1.0;
    }
    return min_ratio * 100.0;
}

namespace {

static bool position_equal(const dto::Position& x, const dto::Position& y)
{
    return x.id == y.id &&
        x.order_id == y.order_id &&
        x.symbol == y.symbol &&
        x.quantity == y.quantity &&
        x.entry_price == y.entry_price &&
        x.is_long == y.is_long &&
        x.unrealized_pnl == y.unrealized_pnl &&
        x.notional == y.notional &&
        x.initial_margin == y.initial_margin &&
        x.maintenance_margin == y.maintenance_margin &&
        x.fee == y.fee &&
        x.leverage == y.leverage &&
        x.fee_rate == y.fee_rate;
}

static bool order_equal(const dto::Order& x, const dto::Order& y)
{
    return x.id == y.id &&
        x.symbol == y.symbol &&
        x.quantity == y.quantity &&
        x.price == y.price &&
        x.side == y.side &&
        x.position_side == y.position_side &&
        x.reduce_only == y.reduce_only &&
        x.closing_position_id == y.closing_position_id;
}

template<typename T, typename Eq>
static bool vec_equal_impl(const std::vector<T>& a, const std::vector<T>& b, Eq eq)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), eq);
}

} // namespace

/// @brief Compare two Position snapshots for exact equality.
/// @param a First positions vector.
/// @param b Second positions vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Position>& a,
    const std::vector<dto::Position>& b) {
    return vec_equal_impl(a, b, position_equal);
}

/// @brief Compare two Order snapshots for exact equality.
/// @param a First orders vector.
/// @param b Second orders vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Order>& a,
    const std::vector<dto::Order>& b) {
    return vec_equal_impl(a, b, order_equal);
}
