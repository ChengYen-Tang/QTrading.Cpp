#include "Signal/PairMarketSignalSupport.hpp"

namespace QTrading::Signal::Support {

bool ResolvePairSymbolIds(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    PairSymbolIds& ids)
{
    if (ids.resolved) {
        return true;
    }
    if (!market || !market->symbols) {
        return false;
    }

    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] == spot_symbol) {
            ids.spot_id = i;
        }
        if (symbols[i] == perp_symbol) {
            ids.perp_id = i;
        }
    }

    const bool spot_ok = ids.spot_id < symbols.size() && symbols[ids.spot_id] == spot_symbol;
    const bool perp_ok = ids.perp_id < symbols.size() && symbols[ids.perp_id] == perp_symbol;
    ids.resolved = spot_ok && perp_ok;
    return ids.resolved;
}

bool MarketHasTradePair(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const PairSymbolIds& ids)
{
    if (!market || !ids.resolved) {
        return false;
    }
    if (ids.spot_id >= market->trade_klines_by_id.size() ||
        ids.perp_id >= market->trade_klines_by_id.size()) {
        return false;
    }

    return market->trade_klines_by_id[ids.spot_id].has_value() &&
        market->trade_klines_by_id[ids.perp_id].has_value();
}

std::optional<double> ComputeBasisPct(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const PairSymbolIds& ids,
    bool use_mark_index)
{
    if (!market || !ids.resolved) {
        return std::nullopt;
    }

    if (use_mark_index &&
        ids.perp_id < market->mark_klines_by_id.size() &&
        ids.perp_id < market->index_klines_by_id.size())
    {
        const auto& mark_opt = market->mark_klines_by_id[ids.perp_id];
        const auto& index_opt = market->index_klines_by_id[ids.perp_id];
        if (mark_opt.has_value() && index_opt.has_value() && index_opt->ClosePrice > 0.0) {
            return (mark_opt->ClosePrice - index_opt->ClosePrice) / index_opt->ClosePrice;
        }
    }

    if (ids.spot_id >= market->trade_klines_by_id.size() ||
        ids.perp_id >= market->trade_klines_by_id.size()) {
        return std::nullopt;
    }

    const auto& spot_opt = market->trade_klines_by_id[ids.spot_id];
    const auto& perp_opt = market->trade_klines_by_id[ids.perp_id];
    if (!spot_opt.has_value() || !perp_opt.has_value() || spot_opt->ClosePrice <= 0.0) {
        return std::nullopt;
    }

    return (perp_opt->ClosePrice - spot_opt->ClosePrice) / spot_opt->ClosePrice;
}

} // namespace QTrading::Signal::Support
