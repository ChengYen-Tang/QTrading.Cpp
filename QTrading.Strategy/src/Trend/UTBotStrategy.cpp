#include "Trend/UTBotStrategy.hpp"
#include <cmath>
#include <numeric>

using namespace QTrading;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::DataPreprocess::Dto;
using namespace QTrading::Strategy;

/// @brief Construct a UTBotStrategy instance.
/// @param ex Exchange interface for order execution.
/// @param key ATR multiplier.
/// @param q  Order quantity.
/// @param useHA Use Heikin-Ashi close if true.
UTBotStrategy::UTBotStrategy(ExchangePtr ex, double key, double q, bool useHA)
    : ex(std::move(ex)), a(key), qty(q), useHA(useHA) {
}

/// @brief Handle incoming aggregated kline data.
/// @param dto AggregateKline containing current and historical bars.
void UTBotStrategy::on_data(const AggPtr& dto)
{
    for (const auto& [sym, bars] : dto->HistoricalKlines)
        if (!bars.empty()) process_symbol(sym, bars);
}

/// @brief Core per-symbol logic: compute ATR, update trailing stop, and place orders.
/// @param sym Symbol identifier.
/// @param bars Deque of KlineDto (index 0 = live bar).
void UTBotStrategy::process_symbol(const std::string& sym,
    const std::deque<KlineDto>& bars)
{
    auto& S = st[sym];
    const KlineDto& live = bars.front();             // still building

    // build srcCurrent / srcPrev (HA or normal)
    const double srcCur = useHA ? ha_close(live) : live.ClosePrice;
    const double srcPrev = S.init ? S.prevSrc :
        (bars.size() > 1
            ? (useHA ? ha_close(bars[1]) : bars[1].ClosePrice)
            : srcCur);

    // ATR from finished bars (skip index 0)
    if (bars.size() < 2) {           // not enough history yet
        S.prevSrc = srcCur; return;
    }
    double sumTR = 0.0;
    for (size_t i = 1; i < bars.size(); ++i)
        sumTR += true_range(bars[i],
            (i + 1 < bars.size() ? &bars[i + 1] : nullptr));
    const int    atrLen = static_cast<int>(bars.size()) - 1;
    const double atr = sumTR / static_cast<double>(atrLen);
    const double nLoss = a * atr;

    // trailing‑stop update (identical logic)
    const double prevTrail = S.init ? S.trailing : srcCur - nLoss;
    double trail;

    if (srcCur > prevTrail && srcPrev > prevTrail)
        trail = std::max(prevTrail, srcCur - nLoss);
    else if (srcCur < prevTrail && srcPrev < prevTrail)
        trail = std::min(prevTrail, srcCur + nLoss);
    else if (srcCur > prevTrail)
        trail = srcCur - nLoss;
    else
        trail = srcCur + nLoss;

    // long / short crossover
    int newPos = S.pos;
    if (srcPrev < prevTrail && srcCur > prevTrail) newPos = 1;
    if (srcPrev > prevTrail && srcCur < prevTrail) newPos = -1;

    if (newPos != S.pos) {
		ex->close_position(sym);  // close existing position
        if (newPos == 1) ex->place_order(sym, qty, false);  // LONG mkt
        if (newPos == -1) ex->place_order(sym, qty, false);  // SHORT mkt
        S.pos = newPos;
    }

    // persist
    S.trailing = trail;
    S.prevSrc = srcCur;
    S.init = true;
}

/// @brief Compute Heikin-Ashi close price.
/// @param k Input KlineDto.
/// @return (open + high + low + close)/4.
double UTBotStrategy::ha_close(const KlineDto& k)
{
    return (k.OpenPrice + k.HighPrice + k.LowPrice + k.ClosePrice) / 4.0;
}

/// @brief Compute true range for ATR.
/// @param cur Current bar.
/// @param prev Previous bar pointer (nullptr if none).
/// @return Maximum of (high-low, |high-prevClose|, |low-prevClose|).
double UTBotStrategy::true_range(const KlineDto& cur, const KlineDto* prev)
{
    double pc = prev ? prev->ClosePrice : cur.OpenPrice;
    return std::max({ cur.HighPrice - cur.LowPrice,
                      std::fabs(cur.HighPrice - pc),
                      std::fabs(cur.LowPrice - pc) });
}
