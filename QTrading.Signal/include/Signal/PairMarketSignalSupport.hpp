#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace QTrading::Signal::Support {

struct PairSymbolIds {
    bool resolved{ false };
    std::size_t spot_id{ 0 };
    std::size_t perp_id{ 0 };
};

bool ResolvePairSymbolIds(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    PairSymbolIds& ids);

bool MarketHasTradePair(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const PairSymbolIds& ids);

std::optional<double> ComputeBasisPct(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const PairSymbolIds& ids,
    bool use_mark_index);

} // namespace QTrading::Signal::Support
