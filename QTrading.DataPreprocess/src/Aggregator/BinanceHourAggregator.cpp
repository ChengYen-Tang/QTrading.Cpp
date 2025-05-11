#include "Aggregator/BinanceHourAggregator.hpp"

using namespace QTrading;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::DataPreprocess;
using namespace QTrading::DataPreprocess::Aggregator;
using namespace QTrading::Utils::Queue;

/* ------------------------------------------------------------------ */
/* ctor                                                               */
/* ------------------------------------------------------------------ */
BinanceHourAggregator::BinanceHourAggregator(
    std::shared_ptr<Infra::Exchanges::IExchange<MinutePtr>> ex,
    std::size_t hoursToKeep)
    : exchange(std::move(ex)),
    inCh(exchange->get_market_channel()),
    keepWindow(std::chrono::hours(hoursToKeep))
{
    this->market_channel =
        std::shared_ptr<Channel<std::shared_ptr<Dto::AggregateKline>>>(
            ChannelFactory::CreateBoundedChannel<
            std::shared_ptr<Dto::AggregateKline>>(16,
                OverflowPolicy::DropOldest));
}

/* ------------------------------------------------------------------ */
/* run – worker thread                                                */
/* ------------------------------------------------------------------ */
void BinanceHourAggregator::run()
{
    while (!stopFlag.load())
    {
        if (inCh->IsClosed() && !inCh->TryReceive().has_value()) break;

        auto minuteOpt = inCh->Receive();
        if (!minuteOpt) continue;
        const auto& minute = minuteOpt.value();

        /* --- absorb each symbol’s minute bar --- */
        for (const auto& [sym, optK] : minute->klines)
            if (optK) absorbMinute(sym, optK.value());

        /* --- push downstream --- */
        auto out = std::make_shared<Dto::AggregateKline>();
        out->CurrentKlines = minute;
        out->HistoricalKlines = cache;
        
        for (const auto& [sym, st] : working)
            if (st.initialised)
                out->HistoricalKlines[sym].push_front(st.bar);

        market_channel->Send(out);
    }
}

/* ------------------------------------------------------------------ */
/* absorb one 1‑minute bar for <symbol>                               */
/* ------------------------------------------------------------------ */
void BinanceHourAggregator::absorbMinute(const std::string& sym,
    const KlineDto& k)
{
    using clock = std::chrono::system_clock;
    const auto oneHour = std::chrono::hours(1);

    auto& ws = working[sym];

    /* first minute ever OR hour rollover --------------------------- */
    if (!ws.initialised || k.CloseDateTime >= ws.hourStart + oneHour)
    {
        if (ws.initialised) {
            cache[sym].push_front(ws.bar);                 // push finished hour
        }

        // ---- start new working hour ----
        ws.hourStart = floorToHour(k.CloseDateTime);
        ws.bar = k;
        ws.bar.Timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                ws.hourStart.time_since_epoch()).count();
        ws.initialised = true;

        pruneOld(sym, ws.hourStart);        // <<–  prune **after** push/start
        return;
    }

    /* still inside the same hour – update running aggregates ------- */
    ws.bar.HighPrice = std::max(ws.bar.HighPrice, k.HighPrice);
    ws.bar.LowPrice = std::min(ws.bar.LowPrice, k.LowPrice);
    ws.bar.ClosePrice = k.ClosePrice;
    ws.bar.Volume += k.Volume;
    ws.bar.QuoteVolume += k.QuoteVolume;
    ws.bar.TradeCount += k.TradeCount;
    ws.bar.CloseTime = k.CloseTime;
    ws.bar.CloseDateTime = k.CloseDateTime;
}

/* ------------------------------------------------------------------ */
/* remove bars older than keepWindow_                                 */
/* ------------------------------------------------------------------ */
void BinanceHourAggregator::pruneOld(
    const std::string& sym,
    const std::chrono::system_clock::time_point& now)
{
    auto& dq = cache[sym];
    while (!dq.empty()) {
        auto oldestStart =
            std::chrono::system_clock::time_point{ std::chrono::milliseconds(dq.back().Timestamp) };
        if (now - oldestStart >= keepWindow)
            dq.pop_back();
        else
            break;
    }
}

/* ------------------------------------------------------------------ */
/* helper – floor to the exact hour                                   */
/* ------------------------------------------------------------------ */
std::chrono::system_clock::time_point
BinanceHourAggregator::floorToHour(const std::chrono::system_clock::time_point& tp)
{
    using namespace std::chrono;
    return time_point_cast<hours>(tp);
}
