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
/// 2) The signal only validates market readiness (spot + perp data available).
/// 3) Execution urgency stays low; funding is earned over time, not a single tick.
class FundingCarrySignalEngine final : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    /// @brief Configuration for funding carry signal.
    struct Config {
        std::string spot_symbol;
        std::string perp_symbol;
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

    bool market_has_symbols(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market);
};

} // namespace QTrading::Signal
