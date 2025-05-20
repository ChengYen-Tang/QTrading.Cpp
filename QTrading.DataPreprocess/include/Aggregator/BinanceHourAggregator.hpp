#pragma once

#include <unordered_map>
#include <deque>
#include <memory>
#include <chrono>

#include "IDataPreprocess.hpp"
#include "Dto/AggregateKline.hpp"
#include "Exchanges/IExchange.h"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::DataPreprocess::Aggregator {

    /// @brief  Aggregates 1-minute Binance MultiKlineDto into per-symbol 1-hour bars.
    /// @details
    ///  - Inherits IDataPreprocess of shared_ptr<AggregateKline>.  
    ///  - Reads minute‐level Klines from the exchange channel.  
    ///  - Maintains a sliding window of past N hours (constructor parameter).
    class BinanceHourAggregator
        : public QTrading::DataPreprocess::IDataPreprocess<std::shared_ptr<Dto::AggregateKline>>
    {
    public:
        using MinutePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

        /// @brief  Constructor.
        /// @param ex            Shared pointer to underlying IExchange emitting minute‐level data.
        /// @param hoursToKeep   Number of past hours to retain in the sliding window.
        BinanceHourAggregator(std::shared_ptr<
            QTrading::Infra::Exchanges::IExchange<MinutePtr>> ex,
            std::size_t hoursToKeep);

    private:
        /// @copydoc IDataPreprocess::run()
        void run() override;

        /// @brief  State for an in‐progress hourly bar.
        struct HourState {
            std::chrono::system_clock::time_point hourStart;   ///< Start time of current hour window
            QTrading::Dto::Market::Binance::KlineDto   bar;       ///< Aggregated bar so far
            bool initialised = false;                          ///< Whether this hour has started
        };

        std::shared_ptr<QTrading::Infra::Exchanges::IExchange<MinutePtr>>  exchange;  ///< Source exchange
        std::shared_ptr<QTrading::Utils::Queue::Channel<MinutePtr>>        inCh;      ///< Input channel
        const std::chrono::hours                                         keepWindow;///< Window size

        std::unordered_map<std::string, HourState>                      working;   ///< Current hour bars per symbol
        std::unordered_map<std::string,
            std::deque<QTrading::Dto::Market::Binance::KlineDto>>       cache;     ///< Finished hours per symbol

        /// @brief  Absorb one 1-minute Kline into the current hourly aggregate.
        /// @param  symbol      Symbol string.
        /// @param  minute      Minute‐level KlineDto to merge.
        void absorbMinute(const std::string& symbol,
            const QTrading::Dto::Market::Binance::KlineDto& minute);

        /// @brief  Round a time_point down to the nearest hour.
        /// @param  tp  The input time_point.
        /// @return     time_point truncated to hour precision.
        static std::chrono::system_clock::time_point
            floorToHour(const std::chrono::system_clock::time_point& tp);

        /// @brief  Prune cached hours older than keepWindow.
        /// @param  symbol  Symbol whose cache to prune.
        /// @param  now     Current hour‐aligned time.
        void pruneOld(const std::string& symbol,
            const std::chrono::system_clock::time_point& now);
    };

}
