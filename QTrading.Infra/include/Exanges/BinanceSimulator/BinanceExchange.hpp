#pragma once
/**
 * BinanceExchange (Simulator)
 * ===========================
 *  • Reads multiple CSV files (one per symbol) via MarketData.
 *  • step() publishes the earliest time-stamp available among symbols.
 *    Missing symbols at that timestamp are sent as std::nullopt.
 *  • If ––and only if–– positions OR orders changed since the last step,
 *    the corresponding channel is updated (debounced push).
 */

#include <unordered_map>
#include <vector>
#include <optional>

#include "Exanges/IExchange.h"
#include "Exanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include "Exanges/BinanceSimulator/Futures/Account.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Infra::Exanges::BinanceSim {

    using MultiKlinePtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    class BinanceExchange final : public QTrading::Infra::Exanges::IExchange<MultiKlinePtr> {
    public:
        /// ctor –– symbolCsv = {{"BTCUSDT","btc.csv"}, ...}
        BinanceExchange(const std::vector<std::pair<std::string, std::string>>& symbolCsv,
            double  init_balance = 1'000'000.0,
            int     vip_level = 0);

        /* IExchange implementation -------------------------------------- */
        void  place_order(const std::string& symbol, double qty, double price,
            bool is_long, bool reduce_only = false) override;
        bool  step() override;

        const std::vector<dto::Position>& get_all_positions()   const override;
        const std::vector<dto::Order>& get_all_open_orders() const override;
		void  close() override;
    private:
        /* ------------- data members ------------- */
        std::unordered_map<std::string, MarketData> md;       // CSV cache
        std::unordered_map<std::string, size_t>     cursor;   // current index per symbol
        Account                           account;  // margin / matching engine

        std::vector<dto::Position> last_pos_snapshot;
        std::vector<dto::Order>    last_ord_snapshot;

        /* ------------- helpers ------------- */
        bool     next_timestamp(uint64_t& ts) const;
        void     build_multikline(uint64_t ts,
            QTrading::Dto::Market::Binance::MultiKlineDto& out);

        static bool vec_equal(const std::vector<dto::Position>& a,
            const std::vector<dto::Position>& b);
        static bool vec_equal(const std::vector<dto::Order>& a,
            const std::vector<dto::Order>& b);

        /* disable copy */
        BinanceExchange(const BinanceExchange&) = delete;
        BinanceExchange& operator=(const BinanceExchange&) = delete;
    };

}
