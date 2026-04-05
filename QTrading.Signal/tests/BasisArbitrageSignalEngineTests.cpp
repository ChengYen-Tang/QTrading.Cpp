#include "Signal/BasisArbitrageSignalEngine.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <vector>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(
    unsigned long long ts,
    bool include_spot,
    bool include_perp,
    double perp_close = 100.5,
    std::optional<double> perp_mark = std::nullopt,
    std::optional<double> perp_index = std::nullopt,
    std::optional<double> perp_funding_rate = std::nullopt)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto spot(ts, 0, 0, 0, 100.0, 0, ts, 0, 0, 0, 0);
    QTrading::Dto::Market::Binance::KlineDto perp(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back("BTCUSDT_SPOT");
    symbols->push_back("BTCUSDT_PERP");
    dto->symbols = symbols;
    dto->trade_klines_by_id.resize(symbols->size());
    dto->funding_by_id.resize(symbols->size());
    dto->trade_klines_by_id[0] =
        include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->trade_klines_by_id[1] =
        include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    dto->mark_klines_by_id.resize(symbols->size());
    dto->index_klines_by_id.resize(symbols->size());
    if (perp_funding_rate.has_value()) {
        dto->funding_by_id[1] = QTrading::Dto::Market::Binance::FundingRateDto(ts, *perp_funding_rate);
    }
    if (perp_mark.has_value()) {
        dto->mark_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, *perp_mark);
    }
    if (perp_index.has_value()) {
        dto->index_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, *perp_index);
    }
    return dto;
}

} // namespace

TEST(BasisArbitrageSignalEngineTests, EmitsBasisStrategyLabel)
{
    QTrading::Signal::BasisArbitrageSignalEngine engine({});
    auto signal = engine.on_market(MakeMarket(1000, true, true));

    EXPECT_EQ(signal.strategy, "basis_arbitrage");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(BasisArbitrageSignalEngineTests, BasisMrZscoreUsesEntryExitHysteresis)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 5.0;
    cfg.basis_mr_std_floor = 1e-6;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 5; ++i) {
        auto s = engine.on_market(MakeMarket(1000 + i, true, true, 100.0));
        EXPECT_EQ(s.status, QTrading::Signal::SignalStatus::Inactive);
    }

    bool observed_active = false;
    for (unsigned long long i = 0; i < 5; ++i) {
        auto s = engine.on_market(MakeMarket(2000 + i, true, true, 103.0));
        if (s.status == QTrading::Signal::SignalStatus::Active) {
            observed_active = true;
            EXPECT_GT(s.confidence, 0.0);
            break;
        }
    }
    EXPECT_TRUE(observed_active);

    bool observed_exit = false;
    for (unsigned long long i = 0; i < 200; ++i) {
        auto s = engine.on_market(MakeMarket(3000 + i, true, true, 100.0));
        if (s.status == QTrading::Signal::SignalStatus::Inactive) {
            observed_exit = true;
            break;
        }
    }
    EXPECT_TRUE(observed_exit);
}

TEST(BasisArbitrageSignalEngineTests, BasisMrCooldownBlocksImmediateReentry)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 5.0;
    cfg.basis_mr_entry_persistence_bars = 1;
    cfg.basis_mr_exit_persistence_bars = 1;
    cfg.basis_mr_cooldown_ms = 10'000;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 5; ++i) {
        (void)engine.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    // Trigger entry.
    bool entered = false;
    for (unsigned long long i = 0; i < 10; ++i) {
        auto s = engine.on_market(MakeMarket(2000 + i, true, true, 103.0));
        if (s.status == QTrading::Signal::SignalStatus::Active) {
            entered = true;
            break;
        }
    }
    ASSERT_TRUE(entered);

    // Trigger exit via reversion.
    bool exited = false;
    for (unsigned long long i = 0; i < 200; ++i) {
        auto s = engine.on_market(MakeMarket(3000 + i, true, true, 100.0));
        if (s.status == QTrading::Signal::SignalStatus::Inactive) {
            exited = true;
            break;
        }
    }
    ASSERT_TRUE(exited);

    // Immediately present entry candidate again, still inside cooldown.
    auto blocked = engine.on_market(MakeMarket(3500, true, true, 103.0));
    EXPECT_EQ(blocked.status, QTrading::Signal::SignalStatus::Inactive);

    // After cooldown elapsed, re-entry can happen again.
    bool reentered = false;
    for (unsigned long long i = 0; i < 10; ++i) {
        auto s = engine.on_market(MakeMarket(14'000 + i, true, true, 103.0));
        if (s.status == QTrading::Signal::SignalStatus::Active) {
            reentered = true;
            break;
        }
    }
    EXPECT_TRUE(reentered);
}

TEST(BasisArbitrageSignalEngineTests, RegimeConfidenceOverlayReducesConfidenceInStressBasis)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config base_cfg{};
    base_cfg.adaptive_confidence_enabled = false;
    base_cfg.adaptive_structure_enabled = false;
    base_cfg.basis_regime_use_mark_index = false;
    base_cfg.basis_regime_window_bars = 20;
    base_cfg.basis_regime_min_samples = 10;
    base_cfg.basis_regime_calm_z = 0.5;
    base_cfg.basis_regime_stress_z = 1.0;
    base_cfg.basis_regime_min_confidence_scale = 0.4;

    auto overlay_cfg = base_cfg;
    overlay_cfg.basis_regime_confidence_enabled = true;

    QTrading::Signal::BasisArbitrageSignalEngine baseline(base_cfg);
    QTrading::Signal::BasisArbitrageSignalEngine with_overlay(overlay_cfg);

    for (unsigned long long i = 0; i < 12; ++i) {
        (void)baseline.on_market(MakeMarket(1000 + i, true, true, 100.0));
        (void)with_overlay.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    const auto baseline_shock = baseline.on_market(MakeMarket(2000, true, true, 103.0));
    const auto overlay_shock = with_overlay.on_market(MakeMarket(2000, true, true, 103.0));

    EXPECT_EQ(baseline_shock.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(overlay_shock.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_LT(overlay_shock.confidence, baseline_shock.confidence);
    EXPECT_GT(overlay_shock.confidence, 0.0);
}

TEST(BasisArbitrageSignalEngineTests, BasisMrIgnoresFundingCarryEntryGateConfig)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.entry_min_funding_rate = 0.01;
    cfg.exit_min_funding_rate = 0.01;
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 5.0;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 5; ++i) {
        (void)engine.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    bool entered = false;
    for (unsigned long long i = 0; i < 5; ++i) {
        const auto signal = engine.on_market(MakeMarket(2000 + i, true, true, 103.0));
        if (signal.status == QTrading::Signal::SignalStatus::Active) {
            entered = true;
            break;
        }
    }

    EXPECT_TRUE(entered);
}

TEST(BasisArbitrageSignalEngineTests, BasisStopAlphaZForcesExitOnExtremeTradeBasis)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 20.0;
    cfg.basis_stop_alpha_z = 2.0;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 5; ++i) {
        (void)engine.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    bool entered = false;
    for (unsigned long long i = 0; i < 10; ++i) {
        const auto signal = engine.on_market(MakeMarket(2000 + i, true, true, 103.0));
        if (signal.status == QTrading::Signal::SignalStatus::Active) {
            entered = true;
            break;
        }
    }
    ASSERT_TRUE(entered);

    const auto stopped = engine.on_market(MakeMarket(3000, true, true, 108.0));
    EXPECT_EQ(stopped.status, QTrading::Signal::SignalStatus::Inactive);
    EXPECT_DOUBLE_EQ(stopped.confidence, 0.0);
}

TEST(BasisArbitrageSignalEngineTests, BasisStopRiskZForcesExitOnExtremeMarkIndexBasis)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 20.0;
    cfg.basis_regime_use_mark_index = true;
    cfg.basis_regime_window_bars = 20;
    cfg.basis_regime_min_samples = 5;
    cfg.basis_stop_risk_z = 2.0;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 10; ++i) {
        (void)engine.on_market(MakeMarket(1000 + i, true, true, 100.0, 100.0, 100.0));
    }

    bool entered = false;
    for (unsigned long long i = 0; i < 20; ++i) {
        const auto signal = engine.on_market(MakeMarket(2000 + i, true, true, 103.0, 100.0, 100.0));
        if (signal.status == QTrading::Signal::SignalStatus::Active) {
            entered = true;
            break;
        }
    }
    ASSERT_TRUE(entered);

    const auto stopped = engine.on_market(MakeMarket(3000, true, true, 103.0, 106.0, 100.0));
    EXPECT_EQ(stopped.status, QTrading::Signal::SignalStatus::Inactive);
    EXPECT_DOUBLE_EQ(stopped.confidence, 0.0);
}

TEST(BasisArbitrageSignalEngineTests, CostAwareGateBlocksWeakNetEdge)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 10.0;
    cfg.basis_cost_gate_enabled = true;
    cfg.basis_cost_edge_threshold_pct = 0.0090;
    cfg.basis_cost_trading_cost_rate_per_leg = 0.0010;
    cfg.basis_cost_include_funding = false;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine engine(cfg);

    for (unsigned long long i = 0; i < 10; ++i) {
        (void)engine.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    bool blocked = false;
    for (unsigned long long i = 0; i < 10; ++i) {
        const auto signal = engine.on_market(MakeMarket(2000 + i, true, true, 100.8));
        if (signal.status == QTrading::Signal::SignalStatus::Inactive) {
            blocked = true;
            EXPECT_DOUBLE_EQ(signal.confidence, 0.0);
            break;
        }
    }

    EXPECT_TRUE(blocked);
}

TEST(BasisArbitrageSignalEngineTests, CostAwareGateCanUseFundingCarryAsEdgeBoost)
{
    QTrading::Signal::BasisArbitrageSignalEngine::Config cfg{};
    cfg.basis_mr_enabled = true;
    cfg.basis_mr_use_mark_index = false;
    cfg.basis_mr_window_bars = 20;
    cfg.basis_mr_min_samples = 5;
    cfg.basis_mr_entry_z = 1.0;
    cfg.basis_mr_exit_z = 0.2;
    cfg.basis_mr_max_abs_z = 10.0;
    cfg.basis_cost_gate_enabled = true;
    cfg.basis_cost_edge_threshold_pct = 0.0100;
    cfg.basis_cost_trading_cost_rate_per_leg = 0.0002;
    cfg.basis_cost_expected_funding_settlements = 1.0;
    cfg.basis_cost_include_funding = true;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;

    QTrading::Signal::BasisArbitrageSignalEngine no_funding(cfg);
    QTrading::Signal::BasisArbitrageSignalEngine with_funding(cfg);

    for (unsigned long long i = 0; i < 10; ++i) {
        (void)no_funding.on_market(MakeMarket(1000 + i, true, true, 100.0));
        (void)with_funding.on_market(MakeMarket(1000 + i, true, true, 100.0));
    }

    const auto blocked =
        no_funding.on_market(MakeMarket(2000, true, true, 101.0, std::nullopt, std::nullopt, std::nullopt));
    const auto allowed =
        with_funding.on_market(MakeMarket(2000, true, true, 101.0, std::nullopt, std::nullopt, 0.0020));

    EXPECT_EQ(blocked.status, QTrading::Signal::SignalStatus::Inactive);
    EXPECT_EQ(allowed.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_GT(allowed.confidence, 0.0);
}
