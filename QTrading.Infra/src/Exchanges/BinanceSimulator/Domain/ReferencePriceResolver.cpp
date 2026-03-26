#include "Exchanges/BinanceSimulator/Domain/ReferencePriceResolver.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

FundingMarkResolution ReferencePriceResolver::ResolveFundingMark(
    const QTrading::Dto::Market::Binance::FundingRateDto& funding,
    const std::optional<QTrading::Dto::Market::Binance::ReferenceKlineDto>& raw_mark_kline) noexcept
{
    FundingMarkResolution out{};
    if (funding.MarkPrice.has_value()) {
        out.has_mark_price = true;
        out.mark_price = *funding.MarkPrice;
        out.mark_price_source = Contracts::ReferencePriceSource::Raw;
        return out;
    }
    if (raw_mark_kline.has_value()) {
        out.has_mark_price = true;
        out.mark_price = raw_mark_kline->ClosePrice;
        out.mark_price_source = Contracts::ReferencePriceSource::Raw;
        return out;
    }
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain

