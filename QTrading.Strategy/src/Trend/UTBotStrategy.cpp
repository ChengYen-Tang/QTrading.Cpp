#include "Trend/UTBotStrategy.hpp"
#include <cmath>

using namespace QTrading;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Strategy;

/* ------------------------------------------------------------------ */
UTBotStrategy::UTBotStrategy(ExchangePtr ex, double a, int c, double qty)
    : keyA(a), atrPeriod(c), orderQty(qty), ex(std::move(ex))
{
}

/* ------------------------------------------------------------------ */
void UTBotStrategy::on_data(const MultiPtr& dto)
{
    for (const auto& [sym, opt] : dto->klines) {
        if (!opt) continue;                // missing minute
        handle_symbol(sym, opt.value());
    }
}

/* ------------------------------------------------------------------ */
void UTBotStrategy::handle_symbol(const std::string& sym,
    const KlineDto& k)
{
    auto& s = st[sym];

    /* ---------- ATR update ---------- */
    double tr = calcTR(k.HighPrice, k.LowPrice, s.prevSrc == 0.0 ? k.ClosePrice : s.prevSrc);
    s.trBuf.push_back(tr);
    if (static_cast<int>(s.trBuf.size()) > atrPeriod) s.trBuf.pop_front();

    if (!s.atrInit) {
        if (static_cast<int>(s.trBuf.size()) == atrPeriod) {
            double sma = 0.0;
            for (double v : s.trBuf) sma += v;
            sma /= atrPeriod;
            s.atr = sma;
            s.atrInit = true;
        }
    }
    else {
        s.atr = rma_next(s.atr, tr, atrPeriod, true, 0.0);
    }

    if (!s.atrInit) { s.prevSrc = k.ClosePrice; return; }   // wait ATR ready

    /* ---------- trailing stop ---------- */
    s.prevTrailing = s.trailing;
    double nLoss = keyA * s.atr;

    if (k.ClosePrice > s.prevTrailing && s.prevSrc > s.prevTrailing)
        s.trailing = std::max(s.prevTrailing, k.ClosePrice - nLoss);
    else if (k.ClosePrice < s.prevTrailing && s.prevSrc < s.prevTrailing)
        s.trailing = std::min(s.prevTrailing, k.ClosePrice + nLoss);
    else if (k.ClosePrice > s.prevTrailing)
        s.trailing = k.ClosePrice - nLoss;
    else
        s.trailing = k.ClosePrice + nLoss;

    /* ---------- position logic ---------- */
    int newPos = s.pos;
    if (s.prevSrc < s.prevTrailing && k.ClosePrice > s.trailing)  newPos = 1;   // long
    if (s.prevSrc > s.prevTrailing && k.ClosePrice < s.trailing)  newPos = -1;   // short

    if (newPos != s.pos) {
        // Close opposite, open new
        if (newPos == 1) {
            ex->place_order(sym, orderQty, 0.0, true, false);   // market long
        }
        else if (newPos == -1) {
            ex->place_order(sym, orderQty, 0.0, false, false);  // market short
        }
        s.pos = newPos;
    }

    /* save previous close */
    s.prevSrc = k.ClosePrice;
}

/* ------------------------------------------------------------------ */
double UTBotStrategy::calcTR(double high, double low, double prevClose)
{
    double c1 = high - low;
    double c2 = std::fabs(high - prevClose);
    double c3 = std::fabs(low - prevClose);
    return std::max({ c1,c2,c3 });
}

double UTBotStrategy::rma_next(double prevAtr, double tr, int len, bool init, double sma)
{
    // Wilder’s RMA
    if (!init) return sma;     // not used, kept for completeness
    return (prevAtr * (len - 1) + tr) / static_cast<double>(len);
}
