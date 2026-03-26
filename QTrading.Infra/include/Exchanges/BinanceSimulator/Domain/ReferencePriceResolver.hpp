#pragma once

#include <optional>

#include "Dto/Market/Binance/FundingRate.hpp"
#include "Dto/Market/Binance/ReferenceKline.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

struct FundingMarkResolution {
    bool has_mark_price{ false };
    double mark_price{ 0.0 };
    Contracts::ReferencePriceSource mark_price_source{ Contracts::ReferencePriceSource::None };
};

class ReferencePriceResolver final {
public:
    static FundingMarkResolution ResolveFundingMark(
        const QTrading::Dto::Market::Binance::FundingRateDto& funding,
        const std::optional<QTrading::Dto::Market::Binance::ReferenceKlineDto>& raw_mark_kline) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain

