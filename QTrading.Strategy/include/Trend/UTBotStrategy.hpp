#pragma once
/**
 * UT‑Bot Alerts   (converted from TradingView Pine v4)
 *
 *  • Works on *1‑minute* source bars contained in
 *    QTrading::Dto::Market::Binance::MultiKlineDto.
 *  • Keeps an ATR(c) (default 10) and trailing‑stop logic identical to
 *    the Pine script.
 *
 *  The strategy:
 *    ▸ Long  : when price crosses *above* the trailing stop
 *    ▸ Short : when price crosses *below* the trailing stop
 *
 *  Order size is fixed (qty parameter, default = 1).
 */
#include <unordered_map>
#include <deque>
#include "IStrategy.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exanges/IExchange.h"

namespace QTrading::Strategy {

    class UTBotStrategy final
        : public IStrategy<QTrading::Dto::Market::Binance::MultiKlineDto>
    {
    public:
        using MultiPtr =
            std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;
        using ExchangePtr = std::shared_ptr<
            QTrading::Infra::Exanges::IExchange<MultiPtr>>;

        UTBotStrategy(ExchangePtr     ex,
            double          keyA = 1.0,   // Pine “a”
            int             atrPeriod = 10,    // Pine “c”
            double          qty = 1.0);

    protected:
        void on_data(const MultiPtr& dto) override;

    private:
        struct SymbolState {
            // Wilder ATR
            std::deque<double> trBuf;
            double  atr = 0.0;
            bool    atrInit = false;

            // trailing stop & signals
            double  trailing = 0.0;
            double  prevTrailing = 0.0;
            double  prevSrc = 0.0;
            int     pos = 0;          // -1 short, 0 flat, 1 long
        };

        /* parameters */
        const double  keyA;
        const int     atrPeriod;
        const double  orderQty;

        /* state per symbol */
        std::unordered_map<std::string, SymbolState> st;

        /* typed exchange ptr for order placing */
        ExchangePtr ex;

        /* helpers */
        static double calcTR(double high, double low, double prevClose);
        static double rma_next(double prevAtr, double tr, int length, bool init, double sma);

        void handle_symbol(const std::string& sym,
            const QTrading::Dto::Market::Binance::KlineDto& k);
    };

} // namespace QTrading::Strategy
