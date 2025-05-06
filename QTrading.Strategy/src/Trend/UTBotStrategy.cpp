#include "Trend/UTBotStrategy.hpp"
#include <cmath>
#include <numeric>

using namespace QTrading;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::DataPreprocess::Dto;
using namespace QTrading::Strategy;

/* ------------------------------------------------------------------ */
UTBotStrategy::UTBotStrategy(ExchangePtr ex, double key, double q, bool useHA)
    : ex(std::move(ex)), a(key), qty(q), useHA(useHA) {
}

/* ------------------------------------------------------------------ */
void UTBotStrategy::on_data(const AggPtr& dto)
{
    for (const auto& [sym, bars] : dto->HistoricalKlines)
        if (!bars.empty()) process_symbol(sym, bars);
}

/* ---------- per‑symbol processing -------------------------------- */
void UTBotStrategy::process_symbol(const std::string& sym,
    const std::deque<KlineDto>& bars)
{
    auto& S = st[sym];
    const KlineDto& live = bars.front();             // still building

    /* --- build srcCurrent / srcPrev (HA or normal) --------------- */
    const double srcCur = useHA ? ha_close(live) : live.ClosePrice;
    const double srcPrev = S.init ? S.prevSrc :
        (bars.size() > 1
            ? (useHA ? ha_close(bars[1]) : bars[1].ClosePrice)
            : srcCur);

    /* --- ATR from finished bars (skip index 0) -------------------- */
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

    /* --- trailing‑stop update (identical logic) ------------------- */
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

    /* --- long / short crossover ---------------------------------- */
    int newPos = S.pos;
    if (srcPrev < prevTrail && srcCur > trail) newPos = 1;
    if (srcPrev > prevTrail && srcCur < trail) newPos = -1;

    if (newPos != S.pos) {
        if (newPos == 1) ex->place_order(sym, qty, 0.0, true);  // LONG mkt
        if (newPos == -1) ex->place_order(sym, qty, 0.0, false);  // SHORT mkt
        S.pos = newPos;
    }

    /* --- persist -------------------------------------------------- */
    S.trailing = trail;
    S.prevSrc = srcCur;
    S.init = true;
}

/* ---------- helpers ---------------------------------------------- */
double UTBotStrategy::ha_close(const KlineDto& k)
{
    return (k.OpenPrice + k.HighPrice + k.LowPrice + k.ClosePrice) / 4.0;
}

/* ------------------------------------------------------------------ */
double UTBotStrategy::true_range(const KlineDto& cur, const KlineDto* prev)
{
    double pc = prev ? prev->ClosePrice : cur.OpenPrice;
    return std::max({ cur.HighPrice - cur.LowPrice,
                      std::fabs(cur.HighPrice - pc),
                      std::fabs(cur.LowPrice - pc) });
}
