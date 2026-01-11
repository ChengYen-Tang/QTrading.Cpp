#include "Aggregator/BinanceHourAggregator.hpp"
#include "Debug/Trace.hpp"

using namespace QTrading;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::DataPreprocess;
using namespace QTrading::DataPreprocess::Aggregator;
using namespace QTrading::Utils::Queue;

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

    QTR_TRACE("agg", "BinanceHourAggregator constructed");
}

void BinanceHourAggregator::run()
{
    QTR_TRACE("agg", "run loop begin");
    while (!stopFlag.load())
    {
        QTR_TRACE("agg", "Receive minute begin");
        auto minuteOpt = inCh->Receive();
        QTR_TRACE("agg", minuteOpt ? "Receive minute got value" : "Receive minute nullopt (closed+empty?)");
        if (!minuteOpt) break; // closed & empty
        const auto& minute = minuteOpt.value();

        // absorb each symbol’s minute bar
        for (const auto& [sym, optK] : minute->klines) {
            if (optK) absorbMinute(sym, optK.value());
        }

        // push downstream
        auto out = std::make_shared<Dto::AggregateKline>();
        out->CurrentKlines = minute;
        out->HistoricalKlines = cache;

        // include in-progress bar at front if initialised
        for (const auto& [sym, st] : working) {
            if (st.initialised)
                out->HistoricalKlines[sym].push_front(st.bar);
        }

        QTR_TRACE("agg", "Send downstream begin");
        market_channel->Send(out);
        QTR_TRACE("agg", "Send downstream end");
    }
    QTR_TRACE("agg", "run loop end");
}

/// @brief  Merge a single minute bar into the hourly aggregate.
void BinanceHourAggregator::absorbMinute(const std::string& sym, const KlineDto& k)
{
    using clock = std::chrono::system_clock;
    const auto oneHour = std::chrono::hours(1);

    auto& ws = working[sym];

    // first minute ever or hour rollover
    if (!ws.initialised || k.CloseDateTime >= ws.hourStart + oneHour)
    {
        if (ws.initialised) {
            cache[sym].push_front(ws.bar);
        }

        ws.hourStart = floorToHour(k.CloseDateTime);
        ws.bar = k;
        ws.bar.Timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                ws.hourStart.time_since_epoch()).count();
        ws.initialised = true;

        pruneOld(sym, ws.hourStart);
        return;
    }

    // still inside the same hour – update running aggregates
    ws.bar.HighPrice = std::max(ws.bar.HighPrice, k.HighPrice);
    ws.bar.LowPrice = std::min(ws.bar.LowPrice, k.LowPrice);
    ws.bar.ClosePrice = k.ClosePrice;
    ws.bar.Volume += k.Volume;
    ws.bar.QuoteVolume += k.QuoteVolume;
    ws.bar.TradeCount += k.TradeCount;
    ws.bar.CloseTime = k.CloseTime;
    ws.bar.CloseDateTime = k.CloseDateTime;
}

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

std::chrono::system_clock::time_point
BinanceHourAggregator::floorToHour(const std::chrono::system_clock::time_point& tp)
{
    using namespace std::chrono;
    return time_point_cast<hours>(tp);
}
