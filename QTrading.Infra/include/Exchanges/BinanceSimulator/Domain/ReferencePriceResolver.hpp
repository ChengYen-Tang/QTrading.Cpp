#pragma once

#include <optional>

#include "Dto/Market/Binance/FundingRate.hpp"
#include "Dto/Market/Binance/ReferenceKline.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

/// Reduced-scope funding mark resolution returned by the reference-price helper.
struct FundingMarkResolution {
    /// True when a mark price was successfully resolved.
    bool has_mark_price{ false };
    /// Resolved mark price value.
    double mark_price{ 0.0 };
    /// Source used to obtain `mark_price`.
    Contracts::ReferencePriceSource mark_price_source{ Contracts::ReferencePriceSource::None };
};

/// Resolves reference prices needed by funding and mark-dependent reduced paths.
class ReferencePriceResolver final {
public:
    // Current kernel keeps a raw-only reference path:
    // - FundingRate.MarkPrice when present
    // - same-step raw mark kline close fallback
    // Interpolation is intentionally not restored in this resolver.
    /// Resolves the mark price associated with a funding row under the reduced raw-only model.
    static FundingMarkResolution ResolveFundingMark(
        const QTrading::Dto::Market::Binance::FundingRateDto& funding,
        const std::optional<QTrading::Dto::Market::Binance::ReferenceKlineDto>& raw_mark_kline) noexcept;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
