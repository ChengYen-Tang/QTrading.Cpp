#include "Exchanges/BinanceSimulator/Account/AccountPolicies.hpp"

#include "Exchanges/BinanceSimulator/Account/Config.hpp"

#include <algorithm>
#include <cmath>

using QTrading::Dto::Market::Binance::KlineDto;
using QTrading::Dto::Trading::OrderSide;

namespace {

struct FillModel {
    Account::KlineVolumeSplitMode split_mode{ Account::KlineVolumeSplitMode::LegacyTotalOnly };

    std::pair<bool, std::pair<double, double>> build_directional_liquidity(const KlineDto& k) const
    {
        const double vol = std::max(0.0, k.Volume);
        if (split_mode == Account::KlineVolumeSplitMode::LegacyTotalOnly || vol <= 0.0) {
            return { false, {0.0, 0.0} };
        }

        bool has = false;
        double buy_liq = 0.0;
        double sell_liq = 0.0;

        if (k.TakerBuyBaseVolume > 0.0) {
            has = true;
            buy_liq = std::clamp(k.TakerBuyBaseVolume, 0.0, vol);
            sell_liq = vol - buy_liq;
        }
        else if (split_mode == Account::KlineVolumeSplitMode::TakerBuyOrHeuristic) {
            const double range_raw = k.HighPrice - k.LowPrice;
            if (std::abs(range_raw) < 1e-12) {
                buy_liq = vol * 0.5;
            }
            else {
                const double range = std::max(1e-12, range_raw);
                double close_loc = (k.ClosePrice - k.LowPrice) / range;
                close_loc = std::clamp(close_loc, 0.0, 1.0);
                buy_liq = vol * close_loc;
            }
            sell_liq = vol - buy_liq;
            has = true;
        }

        return { has, {buy_liq, sell_liq} };
    }
};

struct PriceExecutionModel {
    double market_exec_slippage{};
    double limit_exec_slippage{};

    double execution_price(const Order& ord, const KlineDto& k) const
    {
        const bool is_market = (ord.price <= 0.0);
        double fill_price = is_market ? k.ClosePrice : ord.price;

        if (is_market) {
            const double slip = std::max(0.0, market_exec_slippage);
            if (slip > 0.0) {
                if (ord.side == OrderSide::Buy) {
                    fill_price = std::min(k.HighPrice, k.ClosePrice * (1.0 + slip));
                }
                else {
                    fill_price = std::max(k.LowPrice, k.ClosePrice * (1.0 - slip));
                }
            }
        }
        else {
            const double slip = std::max(0.0, limit_exec_slippage);
            if (slip > 0.0) {
                if (ord.side == OrderSide::Buy) {
                    const double worse = std::min(k.HighPrice, k.ClosePrice * (1.0 + slip));
                    fill_price = std::min(ord.price, worse);
                }
                else {
                    const double worse = std::max(k.LowPrice, k.ClosePrice * (1.0 - slip));
                    fill_price = std::max(ord.price, worse);
                }
            }
        }

        return fill_price;
    }
};

} // namespace

Account::Policies AccountPolicies::Default()
{
    Account::Policies p;

    p.fee_rates = [](int vip_level) -> Account::FeeRates {
        auto it = vip_fee_rates.find(vip_level);
        if (it != vip_fee_rates.end()) {
            return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
        }
        return std::make_tuple(vip_fee_rates.at(0).maker_fee_rate, vip_fee_rates.at(0).taker_fee_rate);
    };

    p.spot_fee_rates = [](int vip_level) -> Account::FeeRates {
        auto it = spot_vip_fee_rates.find(vip_level);
        if (it != spot_vip_fee_rates.end()) {
            return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
        }
        return std::make_tuple(spot_vip_fee_rates.at(0).maker_fee_rate, spot_vip_fee_rates.at(0).taker_fee_rate);
    };

    p.can_fill_and_taker = [](const Order& ord, const KlineDto& k) -> std::pair<bool, bool> {
        const bool is_market = (ord.price <= 0.0);
        if (is_market) {
            return { true, true };
        }

        const bool is_buy = (ord.side == OrderSide::Buy);
        const bool triggered = (is_buy ? (k.LowPrice <= ord.price) : (k.HighPrice >= ord.price));
        if (!triggered) {
            return { false, false };
        }

        const bool marketable_at_close = (is_buy ? (k.ClosePrice <= ord.price) : (k.ClosePrice >= ord.price));
        return { true, marketable_at_close };
    };

    p.directional_liquidity = [](Account::KlineVolumeSplitMode mode, const KlineDto& k)
        -> std::pair<bool, std::pair<double, double>> {
        FillModel m;
        m.split_mode = mode;
        return m.build_directional_liquidity(k);
    };

    p.execution_price = [](const Order& ord, const KlineDto& k, double market_exec_slip, double limit_exec_slip) -> double {
        PriceExecutionModel px;
        px.market_exec_slippage = market_exec_slip;
        px.limit_exec_slippage = limit_exec_slip;
        return px.execution_price(ord, k);
    };

    p.liquidation_price = [](const Position& pos, const KlineDto& k) -> double {
        return pos.is_long ? k.LowPrice : k.HighPrice;
    };

    return p;
}
