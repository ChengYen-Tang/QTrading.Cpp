#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "IIntentBuilder.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Intent {

/// @brief Build a delta-neutral intent for funding carry trades.
/// Design intent:
/// 1) Create a two-leg structure that is delta-neutral (spot + perp).
/// 2) When receive_funding is true, favor the side that collects funding.
/// 3) The intent describes structure only; sizing is handled by the risk engine.
class FundingCarryIntentBuilder : public IIntentBuilder<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for funding carry intent.
    struct Config {
        std::string spot_symbol = "BTCUSDT_SPOT";
        std::string perp_symbol = "BTCUSDT_PERP";
        bool receive_funding = true;
        // Basis-arbitrage optional directional controls (unused by funding-carry path).
        bool basis_directional_enabled = false;
        bool basis_direction_use_mark_index = true;
        double basis_direction_switch_entry_abs_pct = 0.005;
        double basis_direction_switch_exit_abs_pct = 0.0015;
        uint64_t basis_direction_switch_cooldown_ms = 0;
    };

    explicit FundingCarryIntentBuilder(Config cfg);

    virtual TradeIntent build(const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
};

} // namespace QTrading::Intent
