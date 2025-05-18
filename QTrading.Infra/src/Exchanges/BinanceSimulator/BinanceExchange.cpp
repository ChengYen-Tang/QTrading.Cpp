#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Enum/LogModule.hpp"
#include "Dto/AccountLog.hpp"

using namespace QTrading;
using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
using namespace QTrading::Log;

/* ------------------------------------------------------------------ */
/* ctor                                                                */
/* ------------------------------------------------------------------ */
BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    double init_balance, int vip_level)
    : BinanceExchange(symbolCsv, logger, std::make_shared<Account>(init_balance, vip_level)) { }

BinanceExchange::BinanceExchange(
    const std::vector<std::pair<std::string, std::string>>& symbolCsv,
    std::shared_ptr<QTrading::Log::Logger> logger,
    std::shared_ptr<Account> account)
	: logger(logger), account(account)
{
    /* preload CSV & initialise cursors */
    for (const auto& [sym, csv] : symbolCsv) {
        md.emplace(sym, MarketData(sym, csv));
        cursor[sym] = 0;
    }

    /* create channels */
    market_channel.reset(ChannelFactory::CreateBoundedChannel<MultiKlinePtr>(8, OverflowPolicy::DropOldest));
    position_channel.reset(ChannelFactory::CreateBoundedChannel<std::vector<dto::Position>>(4, OverflowPolicy::DropOldest));
    order_channel.reset(ChannelFactory::CreateBoundedChannel<std::vector<dto::Order>>(4, OverflowPolicy::DropOldest));
}

/* ------------------------------------------------------------------ */
/* trading commands                                                    */
/* ------------------------------------------------------------------ */
void BinanceExchange::place_order(const std::string& sym, double q, double p,
    bool is_long, bool reduce_only)
{
    account->place_order(sym, q, p, is_long, reduce_only);
}

/* ------------------------------------------------------------------ */
/* step –– advance one bar                                             */
/* ------------------------------------------------------------------ */
bool BinanceExchange::step()
{
    log_status();
    uint64_t ts;
    if (!next_timestamp(ts)) {       /* out of data –– close channels */
        market_channel->Close();
        position_channel->Close();
        order_channel->Close();
        return false;
    }

    /* 1) build & push market data */
	set_global_timestamp(ts);
    auto dto = std::make_shared<MultiKlineDto>();
    build_multikline(ts, *dto);
    market_channel->Send(dto);    // always emit market

    /* 2) debounce position / order updates */
    const auto& curP = account->get_all_positions();
    if (!vec_equal(curP, last_pos_snapshot)) {
        position_channel->Send(curP);
        last_pos_snapshot = curP;
    }
    const auto& curO = account->get_all_open_orders();
    if (!vec_equal(curO, last_ord_snapshot)) {
        order_channel->Send(curO);
        last_ord_snapshot = curO;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* snapshots                                                           */
/* ------------------------------------------------------------------ */
const std::vector<dto::Position>& BinanceExchange::get_all_positions() const {
    return account->get_all_positions();
}
const std::vector<dto::Order>& BinanceExchange::get_all_open_orders() const {
    return account->get_all_open_orders();
}

void BinanceExchange::close()
{
    for (auto& [s, idx] : cursor) 
        idx = md.at(s).get_klines_count();

	IExchange<MultiKlinePtr>::close();
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
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

void BinanceExchange::build_multikline(uint64_t ts, MultiKlineDto& out)
{
    out.Timestamp = ts;
    out.klines.clear();

    std::unordered_map<std::string, std::pair<double, double>> priceVol;

    for (auto& [sym, data] : md) {
        size_t idx = cursor[sym];
        if (idx < data.get_klines_count() &&
            data.get_kline(idx).Timestamp == ts)
        {
            const auto& k = data.get_kline(idx);
            out.klines.emplace(sym, k);
            priceVol.emplace(sym, std::make_pair(k.ClosePrice, k.Volume));
            ++cursor[sym];
        }
        else {
            out.klines.emplace(sym, std::nullopt);
        }
    }
    if (!priceVol.empty())
        account->update_positions(priceVol);
}

void BinanceExchange::log_status() {
    auto account_log = std::make_shared<AccountLog>(AccountLog{ account->get_balance(), account->total_unrealized_pnl(), account->get_equity() });
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

template<typename T>
static bool vec_compare(const std::vector<T>& a, const std::vector<T>& b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](const T& x, const T& y) { return memcmp(&x, &y, sizeof(T)) == 0; });
}

bool BinanceExchange::vec_equal(const std::vector<dto::Position>& a,
    const std::vector<dto::Position>& b) {
    return vec_compare(a, b);
}

bool BinanceExchange::vec_equal(const std::vector<dto::Order>& a,
    const std::vector<dto::Order>& b) {
    return vec_compare(a, b);
}
