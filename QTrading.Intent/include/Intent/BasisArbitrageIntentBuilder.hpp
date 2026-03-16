#pragma once

#include "Intent/FundingCarryIntentBuilder.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>

namespace QTrading::Intent {

/// @brief Basis-arbitrage intent builder.
/// Reuses the proven two-leg carry structure while using basis-specific labels.
class BasisArbitrageIntentBuilder : public FundingCarryIntentBuilder {
public:
    using Config = FundingCarryIntentBuilder::Config;

    explicit BasisArbitrageIntentBuilder(Config cfg);

    TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    bool current_receive_funding_{ true };
    uint64_t last_direction_switch_ts_{ 0 };
    bool has_symbol_ids_{ false };
    std::size_t spot_id_{ 0 };
    std::size_t perp_id_{ 0 };

    bool ResolveSymbolIds(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market);
    std::optional<double> ComputeBasisPct(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) const;
    void ApplyLegDirection(TradeIntent& intent, bool receive_funding) const;
};

} // namespace QTrading::Intent
