#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"

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
    /// @details Preload each CSV into MarketData and initialize the cursor.
    for (const auto& [sym, csv] : symbolCsv) {
        md.emplace(sym, MarketData(sym, csv));
        cursor[sym] = 0;
    }

    /// @details Create bounded channels:
    ///          - market_channel: 8-element sliding window of MultiKlineDto
    ///          - position_channel / order_channel: 4-element debounce buffers
    market_channel.reset(ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(8, OverflowPolicy::DropOldest));
    position_channel.reset(ChannelFactory::CreateBoundedChannel<std::vector<dto::Position>>(4, OverflowPolicy::DropOldest));
    order_channel.reset(ChannelFactory::CreateBoundedChannel<std::vector<dto::Order>>(4, OverflowPolicy::DropOldest));
}

void BinanceExchange::place_order(const std::string& symbol,
    double quantity, double price, bool is_long, bool reduce_only)
{
    account->place_order(symbol, quantity, price, is_long, reduce_only);
}

void BinanceExchange::place_order(const std::string& symbol, double quantity, bool is_long, bool reduce_only)
{
    account->place_order(symbol, quantity, is_long, reduce_only);
}

void BinanceExchange::close_position(const std::string& symbol, double price)
{
    account->close_position(symbol, price);
}

void BinanceExchange::close_position(const std::string& symbol)
{
    account->close_position(symbol);
}

void BinanceExchange::close_position(const std::string& symbol, bool is_long, double price)
{
    account->close_position(symbol, is_long, price);
}

/// @brief Advance one bar of simulation: emit market data & debounced updates.
/// @return true if new data was emitted; false once all CSV data is exhausted.
bool BinanceExchange::step()
{
    log_status();

    // Terminate simulation (end backtest) once wallet balance is depleted.
    if (account->get_balance().WalletBalance <= 0.0) {
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    uint64_t ts;
    /// @details Find the next minimum timestamp among all symbols.
    if (!next_timestamp(ts)) {
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    /// @details Publish multi-symbol market data at timestamp `ts`.
    set_global_timestamp(ts);
    auto dto = std::make_shared<MultiKlineDto>();
    build_multikline(ts, *dto);
    market_channel->Send(dto);    // always emit market

    /// @details Debounce position updates: only send when changed.
    const auto& curP = account->get_all_positions();
    if (!vec_equal(curP, last_pos_snapshot)) {
        position_channel->Send(curP);
        last_pos_snapshot = curP;
    }

    /// @details Debounce order updates: only send when changed.
    const auto& curO = account->get_all_open_orders();
    if (!vec_equal(curO, last_ord_snapshot)) {
        order_channel->Send(curO);
        last_ord_snapshot = curO;
    }
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
bool BinanceExchange::next_timestamp(uint64_t& ts) const
{
    ts = std::numeric_limits<uint64_t>::max();
    bool any = false;
    for (auto& [sym, data] : md) {
        size_t idx = cursor.at(sym);
        if (idx >= data.get_klines_count()) continue;
        ts = std::min<uint64_t>(ts, data.get_kline(idx).Timestamp);
        any = true;
    }
    return any;
}

/// @brief Build and send a MultiKlineDto for timestamp `ts`.
/// @param ts   Global timestamp to align on.
/// @param out  DTO to populate with per-symbol optional KlineDto.
void BinanceExchange::build_multikline(uint64_t ts, MultiKlineDto& out)
{
    out.Timestamp = ts;
    out.klines.clear();

    std::unordered_map<std::string, KlineDto> klineSnap;

    for (auto& [sym, data] : md) {
        size_t idx = cursor[sym];
        if (idx < data.get_klines_count() &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            out.klines.emplace(sym, k);
            klineSnap.emplace(sym, k);
            ++cursor[sym];
        }
        else {
            out.klines.emplace(sym, std::nullopt);
        }
    }
    if (!klineSnap.empty())
        account->update_positions(klineSnap);
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

/// @brief Compare two vectors element-wise via raw memory.
/// @tparam T Element type (Position or Order).
/// @param a First vector.
/// @param b Second vector.
/// @return true if sizes match and all bytes compare equal.
template<typename T>
static bool vec_compare(const std::vector<T>& a, const std::vector<T>& b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](const T& x, const T& y) { return memcmp(&x, &y, sizeof(T)) == 0; });
}

/// @brief Compare two Position snapshots for exact equality.
/// @param a First positions vector.
/// @param b Second positions vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Position>& a,
    const std::vector<dto::Position>& b) {
    return vec_compare(a, b);
}

/// @brief Compare two Order snapshots for exact equality.
/// @param a First orders vector.
/// @param b Second orders vector.
/// @return true if identical.
bool BinanceExchange::vec_equal(const std::vector<dto::Order>& a,
    const std::vector<dto::Order>& b) {
    return vec_compare(a, b);
}
