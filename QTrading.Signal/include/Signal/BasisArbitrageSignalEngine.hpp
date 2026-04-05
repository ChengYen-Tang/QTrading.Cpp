#pragma once

#include "Signal/FundingCarrySignalEngine.hpp"
#include "Signal/ISignalEngine.hpp"
#include "Signal/PairMarketSignalSupport.hpp"

#include <cstddef>
#include <deque>
#include <optional>

namespace QTrading::Signal {

/// @brief Basis-arbitrage signal engine built on top of FundingCarrySignalEngine.
/// This class keeps the same runtime behavior as funding carry for now,
/// but exposes a separate strategy identity for chapter-2 basis research.
class BasisArbitrageSignalEngine : public ISignalEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    using Config = FundingCarrySignalEngine::Config;

    explicit BasisArbitrageSignalEngine(Config cfg);

    SignalDecision on_market(
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    Config cfg_;
    QTrading::Signal::Support::PairSymbolIds symbol_ids_{};
    std::deque<double> basis_window_{};
    std::deque<double> basis_regime_window_{};
    bool alpha_ema_initialized_{ false };
    double basis_alpha_ema_short_{ 0.0 };
    double basis_alpha_ema_mid_{ 0.0 };
    double basis_alpha_ema_long_{ 0.0 };
    bool mr_active_{ false };
    uint32_t mr_entry_streak_{ 0 };
    uint32_t mr_exit_streak_{ 0 };
    uint64_t mr_last_exit_ts_{ 0 };

    bool ResolveSymbolIds(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market);
    std::optional<double> ComputeBasisPct(const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market, bool use_mark_index);
};

} // namespace QTrading::Signal
