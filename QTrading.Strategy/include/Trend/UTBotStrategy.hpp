#pragma once

#include <unordered_map>
#include "IStrategy.hpp"
#include "Dto/AggregateKline.hpp"
#include "Exchanges/IExchange.h"

namespace QTrading::Strategy {

    /// @brief UT-Bot hourly trading strategy.
    /// @details 
    ///  - Consumes `AggregateKline` from a `BinanceHourAggregator`.  
    ///  - Computes ATR over finished hourly bars.  
    ///  - Uses the building bar (index 0) as live price.  
    ///  - Sends market orders on trailing-stop crossovers.
    class UTBotStrategy final
        : public IStrategy<QTrading::DataPreprocess::Dto::AggregateKline>
    {
    public:
        using AggPtr = std::shared_ptr<QTrading::DataPreprocess::Dto::AggregateKline>; ///< Aggregated klines.
        using MultiPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>; ///< Raw 1-min klines.
        using ExchangePtr = std::shared_ptr<
            QTrading::Infra::Exchanges::IExchange<MultiPtr>>; ///< Exchange interface.

        /// @brief Construct the UT-Bot strategy.
        /// @param ex Shared pointer to exchange for order placement.
        /// @param key ATR multiplier ("a" in Pine script).  
        /// @param quantity Order size for each market order.  
        /// @param useHeikinAshi If true, compute source price using Heikin-Ashi close.
        UTBotStrategy(ExchangePtr ex,
            double       key = 1.0,
            double       quantity = 1.0,
            bool         useHeikinAshi = false);

    protected:
        /// @brief Called on each incoming `AggregateKline`.
        /// @param dto Shared pointer to aggregated and historical bars.
        void on_data(const AggPtr& dto) override;

    private:
        /// @brief Internal per-symbol state.
        struct SymState {
            double trailing = 0.0; ///< Current trailing stop level.
            double prevSrc = 0.0; ///< Previous source price.
            int    pos = 0;   ///< Position: -1 short, 0 flat, 1 long.
            bool   init = false; ///< True once initial state is set.
        };

        ExchangePtr                               ex;    ///< Exchange interface.
        const double                              a;     ///< ATR multiplier.
        const double                              qty;   ///< Order quantity.
        const bool                                useHA; ///< Use Heikin-Ashi if true.
        std::unordered_map<std::string, SymState> st;    ///< Map from symbol to state.

        /// @brief Process a single symbol's bar history and manage orders.
        /// @param sym Symbol identifier.
        /// @param bars Deque of `KlineDto` (bars[0] = live, rest = finished).
        void process_symbol(const std::string& sym,
            const std::deque<QTrading::Dto::Market::Binance::KlineDto>& bars);

        /// @brief Compute true range for ATR calculation.
        /// @param cur Current bar.
        /// @param prev Previous bar pointer, or nullptr if none.
        /// @return True range.
        static double true_range(const QTrading::Dto::Market::Binance::KlineDto& cur,
            const QTrading::Dto::Market::Binance::KlineDto* prev);

        /// @brief Compute Heikin-Ashi close price.
        /// @param k Input bar.
        /// @return Heikin-Ashi close.
        static double ha_close(const QTrading::Dto::Market::Binance::KlineDto& k);
    };
}
