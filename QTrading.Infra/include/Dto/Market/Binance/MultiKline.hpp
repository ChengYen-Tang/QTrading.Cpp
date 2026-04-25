#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Dto/Market/Base.hpp"
#include "Dto/Market/Binance/FundingRate.hpp"
#include "Dto/Market/Binance/Kline.hpp"
#include "Dto/Market/Binance/ReferenceKline.hpp"

namespace QTrading::Dto::Market::Binance {

    /// @brief DTO containing multiple symbols 1-minute klines at a given timestamp.
    struct MultiKlineDto : QTrading::Dto::Market::BaseMarketDto {
        /// @brief Stable symbol table (same order as trade_klines_by_id).
        std::shared_ptr<const std::vector<std::string>> symbols;
        /// @brief Per-symbol trade kline data aligned to symbols (index-based).
        std::vector<std::optional<TradeKlineDto>> trade_klines_by_id;
        /// @brief Per-symbol mark reference kline data aligned to symbols (index-based).
        std::vector<std::optional<ReferenceKlineDto>> mark_klines_by_id;
        /// @brief Per-symbol index reference kline data aligned to symbols (index-based).
        std::vector<std::optional<ReferenceKlineDto>> index_klines_by_id;
        /// @brief Latest known funding snapshot per symbol aligned to symbols.
        /// @details This is piecewise-constant between funding updates (e.g., 8h cadence).
        std::vector<std::optional<FundingRateDto>> funding_by_id;
    };

}  // namespace QTrading::Dto::Market::Binance
