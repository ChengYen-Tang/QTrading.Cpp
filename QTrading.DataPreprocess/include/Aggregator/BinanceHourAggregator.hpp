#pragma once
/**
 * BinanceHourAggregator
 * ---------------------
 *  � Inherits IDataPreprocess< shared_ptr<AggregateKline> >
 *  � Reads 1-minute MultiKline from BinanceExchange
 *  � Aggregates per-symbol 1-hour bars (using existing KlineDto)
 *  � Keeps a sliding window of N hours   (ctor parameter)
 */

#include <unordered_map>
#include <deque>
#include <memory>

#include "IDataPreprocess.hpp"
#include "Dto/AggregateKline.hpp"
#include "Exchanges/IExchange.h"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::DataPreprocess::Aggregator {

    class BinanceHourAggregator
        : public QTrading::DataPreprocess::IDataPreprocess<std::shared_ptr<Dto::AggregateKline>>
    {
    public:
        using MinutePtr =
            std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

        BinanceHourAggregator(std::shared_ptr<
            QTrading::Infra::Exchanges::IExchange<MinutePtr>> ex,
            std::size_t hoursToKeep);

    private:
        /* ==== IDataPreprocess ==== */
        void run() override;

        /* ------------- state -------------- */
        struct HourState {
            std::chrono::system_clock::time_point hourStart;
            QTrading::Dto::Market::Binance::KlineDto bar;
            bool initialised = false;
        };

        /* ==== members ==== */
        std::shared_ptr<QTrading::Infra::Exchanges::IExchange<MinutePtr>>  exchange;
        std::shared_ptr<QTrading::Utils::Queue::Channel<MinutePtr>>      inCh;

        const std::chrono::hours keepWindow;

        std::unordered_map<std::string, HourState>                                working;
        std::unordered_map<std::string,
            std::deque<QTrading::Dto::Market::Binance::KlineDto>>                   cache;

        /* ---------- helpers ----------------- */
        void absorbMinute(const std::string& symbol,
            const QTrading::Dto::Market::Binance::KlineDto& minute);

        static std::chrono::system_clock::time_point
            floorToHour(const std::chrono::system_clock::time_point& tp);

        void pruneOld(const std::string & symbol,
            const std::chrono::system_clock::time_point & now);
    };

}
