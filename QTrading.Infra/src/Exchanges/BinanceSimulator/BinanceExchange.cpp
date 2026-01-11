#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"
#include "Debug/Trace.hpp"

#include <future>

using namespace QTrading;
using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
using namespace QTrading::Log;


/// @brief Simulator for Binance futures exchange.
/// @details Reads multiple CSV files (one per symbol), publishes multi-symbol 1-minute bars,
///          updates positions/orders only when they change, and maintains a global timestamp.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level)
    : BinanceExchange(symbolCsv, logger, std::make_shared<Account>(init_balance, vip_level)) { }

/// @brief Primary constructor with an external Account instance.
/// @param symbolCsv  Vector of (symbol, csv_file) pairs to drive market data.
/// @param logger     Shared Logger for writing account/position/order logs.
/// @param account    Shared Account simulation object.
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account)
    : logger(logger), account(account)
{
    // Load each CSV in parallel (safe: each MarketData instance is independent).
    std::vector<std::pair<std::string, std::future<MarketData>>> jobs;
    jobs.reserve(symbolCsv.size());

    for (const auto& [sym, csv] : symbolCsv) {
        jobs.emplace_back(sym, std::async(std::launch::async, [sym, csv]() {
            return MarketData(sym, csv);
            }));
        cursor[sym] = 0;
    }

    // Collect results deterministically by symbol and initialize heap state.
    for (auto& [sym, fut] : jobs) {
        md.emplace(sym, fut.get());

        const auto& data = md.at(sym);
        if (cursor.at(sym) < data.get_klines_count()) {
            const uint64_t ts = data.get_kline(cursor.at(sym)).Timestamp;
            next_ts_by_symbol_[sym] = ts;
            next_ts_heap_.push(HeapItem{ ts, sym });
        }
    }

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
    return account->place_order(symbol, quantity, price, side, position_side, reduce_only);
}

bool BinanceExchange::place_order(const std::string& symbol,
    double quantity,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only)
{
    return account->place_order(symbol, quantity, side, position_side, reduce_only);
}

void BinanceExchange::close_position(const std::string& symbol, double price)
{
    account->close_position(symbol, price);
}

void BinanceExchange::close_position(const std::string& symbol)
{
    account->close_position(symbol);
}

void BinanceExchange::close_position(const std::string& symbol,
    QTrading::Dto::Trading::PositionSide position_side,
    double price)
{
    account->close_position(symbol, position_side, price);
}

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    QTR_TRACE("ex", "step begin");

    log_status();

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
    set_global_timestamp(ts);
    auto dto = std::make_shared<MultiKlineDto>();
    build_multikline(ts, *dto);

    QTR_TRACE("ex", "market_channel Send begin");
    market_channel->Send(dto);
    QTR_TRACE("ex", "market_channel Send end");

    const uint64_t cur_ver = account->get_state_version();

    // Only consider sending snapshots when account reported a state transition.
    if (cur_ver != last_account_version_) {
        const auto& curP = account->get_all_positions();
        if (!vec_equal(curP, last_pos_snapshot)) {
            QTR_TRACE("ex", "position_channel Send");
            position_channel->Send(curP);
            last_pos_snapshot = curP;
        }

        const auto& curO = account->get_all_open_orders();
        if (!vec_equal(curO, last_ord_snapshot)) {
            QTR_TRACE("ex", "order_channel Send");
            order_channel->Send(curO);
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

/// @brief Close the simulator: drain CSVs and close all channels.
/// @details Advances each symbol’s cursor to the end before closing.
void BinanceExchange::close()
{
    for (auto& [s, idx] : cursor) 
        idx = md.at(s).get_klines_count();

	IExchange<MultiKlinePtr>::close();
}

/// @brief Determine the next global timestamp to emit.
/// @param[out] ts  The minimum upcoming timestamp among all symbols.
/// @return true if at least one symbol has data remaining.
bool BinanceExchange::next_timestamp(uint64_t& ts)
{
    // Pop stale heap entries until top matches current per-symbol next_ts.
    while (!next_ts_heap_.empty()) {
        const auto top = next_ts_heap_.top();
        auto it = next_ts_by_symbol_.find(top.sym);
        if (it != next_ts_by_symbol_.end() && it->second == top.ts) {
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
    out.klines.reserve(md.size());

    kline_snap_cache_.clear();
    kline_snap_cache_.reserve(md.size());

    for (auto& [sym, data] : md) {
        size_t idx = cursor[sym];
        if (idx < data.get_klines_count() &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            out.klines.emplace(sym, k);
            kline_snap_cache_.emplace(sym, k);
            ++cursor[sym];

            // Advance this symbol in the multiway merge heap.
            if (cursor[sym] < data.get_klines_count()) {
                const uint64_t next_ts = data.get_kline(cursor[sym]).Timestamp;
                next_ts_by_symbol_[sym] = next_ts;
                next_ts_heap_.push(HeapItem{ next_ts, sym });
            }
            else {
                next_ts_by_symbol_.erase(sym);
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
    auto snap = account->get_balance();
    auto account_log = std::make_shared<AccountLog>(AccountLog{ snap.WalletBalance, snap.UnrealizedPnl, snap.MarginBalance });
	logger->Log(LogModuleToString(LogModule::Account), account_log);
    const auto& curP = account->get_all_positions();
    for (const auto& p : curP) {
        auto pos_log = std::make_shared<Position>(p);
        logger->Log(LogModuleToString(LogModule::Position), pos_log);
	}
    const auto& curO = account->get_all_open_orders();
    for (const auto& o : curO) {
        auto ord_log = std::make_shared<Order>(o);
        logger->Log(LogModuleToString(LogModule::Order), ord_log);
    }
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
