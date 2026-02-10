#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include "ISignalEngine.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Signal {

/// @brief Funding carry signal (always-on delta-neutral carry stance).
/// Design intent:
/// 1) Funding carry is a position-based strategy, not a timing strategy.
/// 2) The signal validates market readiness (spot + perp data available).
/// 3) Guardrails (funding/basis thresholds) prevent holding during unfavorable regimes.
/// 4) Execution urgency stays low; funding is earned over time, not a single tick.
class FundingCarrySignalEngine final : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for funding carry signal.
    struct Config {
        std::string spot_symbol;
        std::string perp_symbol;
        double entry_min_funding_rate = 0.0;
        double exit_min_funding_rate = 0.0;
        /// @brief Emergency funding floor; when current observed funding <= this threshold,
        ///        position exits immediately (ignores min_hold). Set <= -1 to disable.
        double hard_negative_funding_rate = -1.0;
        double entry_max_basis_pct = 1.0;
        double exit_max_basis_pct = 1.0;
        uint64_t cooldown_ms = 0;
        /// @brief Minimum holding time once entered (ms).
        uint64_t min_hold_ms = 8ull * 60ull * 60ull * 1000ull;
    };

    explicit FundingCarrySignalEngine(Config cfg);

    /// @brief Update signal based on latest market snapshot.
    SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    bool has_symbol_ids_{ false };
    std::size_t spot_id_{ 0 };
    std::size_t perp_id_{ 0 };
    bool active_{ false };
    uint64_t last_exit_ts_{ 0 };
    uint64_t active_since_ts_{ 0 };
    bool funding_proxy_initialized_{ false };
    double funding_proxy_ema_{ 0.0 };

    bool market_has_symbols(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market);
};

} // namespace QTrading::Signal
