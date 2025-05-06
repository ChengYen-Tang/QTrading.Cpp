#pragma once
#include <unordered_map>
#include "IStrategy.hpp"
#include "Dto/AggregateKline.hpp"
#include "Exanges/IExchange.h"

namespace QTrading::Strategy {

    /**
     * UT‑Bot Alerts   – hourly variant
     * --------------------------------
     * • Consumes AggregateKline coming from a BinanceHourAggregator.
     * • Computes ATR from the FINISHED hourly bars only.
     * • Uses the still‑building bar (index 0 in deque) as the live price.
     */
    class UTBotStrategy final
        : public IStrategy<QTrading::DataPreprocess::Dto::AggregateKline>
    {
    public:
        using AggPtr = std::shared_ptr<QTrading::DataPreprocess::Dto::AggregateKline>;
        using MultiPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;
        using ExchangePtr = std::shared_ptr<
            QTrading::Infra::Exanges::IExchange<MultiPtr>>;

        UTBotStrategy(ExchangePtr  ex,
            double       key = 1.0,   // Pine “a”
            double       quantity = 1.0,
            bool         useHeikinAshi = false);  // fixed order size

    protected:
        void on_data(const AggPtr& dto) override;

    private:
        struct SymState {
            double trailing = 0.0;
            double prevSrc  = 0.0;
            int    pos = 0;     // -1 short, 0 flat, 1 long
            bool   init = false;
        };

        /* ---- params & handles ---- */
        ExchangePtr                                   ex;
        const double                                  a;     // key value
        const double                                  qty;
        const bool                                    useHA;
        std::unordered_map<std::string, SymState>     st;

        /* ---- helpers ---- */
        static double true_range(const QTrading::Dto::Market::Binance::KlineDto& cur,
            const QTrading::Dto::Market::Binance::KlineDto* prev);
        static double ha_close(const QTrading::Dto::Market::Binance::KlineDto& k);
        void process_symbol(const std::string& sym,
            const std::deque<QTrading::Dto::Market::Binance::KlineDto>& bars);
    };
}
