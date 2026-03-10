#include "Signal/FundingCarrySignalEngine.hpp"

#include <optional>
#include <gtest/gtest.h>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(
    unsigned long long ts,
    bool include_spot,
    bool include_perp,
    double perp_close = 100.5,
    std::optional<double> perp_funding_rate = std::nullopt,
    std::optional<unsigned long long> perp_funding_time = std::nullopt)
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
    dto->trade_klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->trade_klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    if (perp_funding_rate.has_value()) {
        const auto funding_ts = perp_funding_time.value_or(ts);
        dto->funding_by_id[1] = QTrading::Dto::Market::Binance::FundingRateDto(funding_ts, *perp_funding_rate);
    }
    return dto;
}

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarketCustomSymbols(
    unsigned long long ts,
    const std::string& spot_symbol,
    const std::string& perp_symbol,
    bool include_spot,
    bool include_perp,
    double spot_close = 100.0,
    double perp_close = 100.5)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto spot(ts, 0, 0, 0, spot_close, 0, ts, 0, 0, 0, 0);
    QTrading::Dto::Market::Binance::KlineDto perp(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(spot_symbol);
    symbols->push_back(perp_symbol);
    dto->symbols = symbols;
    dto->trade_klines_by_id.resize(symbols->size());
    dto->trade_klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->trade_klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    return dto;
}

} // namespace

TEST(FundingCarrySignalEngineTests, ActiveWhenBothSymbolsPresent)
{
    QTrading::Signal::FundingCarrySignalEngine engine({});
    auto signal = engine.on_market(MakeMarket(1000, true, true));
    EXPECT_EQ(signal.strategy, "funding_carry");
    EXPECT_EQ(signal.symbol, "BTCUSDT_PERP");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, InactiveWhenMissingSymbol)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, -1.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(MakeMarket(1000, true, false));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, InactiveWhenFundingBelowThreshold)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.0001, -1.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(MakeMarket(1000, true, true, 100.02));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, ExitAndCooldown)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.00005, -1.0, 0.01, 0.02, 1000, 0 });
    auto s1 = engine.on_market(MakeMarket(1000, true, true, 100.5));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);

    auto s2 = engine.on_market(MakeMarket(28'801'000, true, true, 110.0));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Inactive);

    auto s3 = engine.on_market(MakeMarket(28'801'700, true, true, 100.5));
    EXPECT_EQ(s3.status, QTrading::Signal::SignalStatus::Inactive);

    auto s4 = engine.on_market(MakeMarket(144'001'000, true, true, 100.5));
    EXPECT_EQ(s4.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, ActiveWithCustomSymbolNames)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_CASH", "BTCUSDT_SWAP", 0.0, 0.0, -1.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(
        MakeMarketCustomSymbols(1000, "BTCUSDT_CASH", "BTCUSDT_SWAP", true, true, 100.0, 100.5));
    EXPECT_EQ(signal.strategy, "funding_carry");
    EXPECT_EQ(signal.symbol, "BTCUSDT_SWAP");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, ConfidenceDropsWhenBasisApproachesExitBand)
{
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.0001, -1.0, 0.02, 0.03, 0 });

    auto s1 = engine.on_market(MakeMarket(1000, true, true, 100.5));   // basis 0.5%
    auto s2 = engine.on_market(MakeMarket(2000, true, true, 102.5));   // basis 2.5%

    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_GT(s1.confidence, s2.confidence);
    EXPECT_GT(s2.confidence, 0.0);
}

TEST(FundingCarrySignalEngineTests, UsesObservedFundingRateFromMarketWhenAvailable)
{
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.0001, 1.0, 1.0, 0 });

    // Basis proxy is tiny and would be below entry threshold,
    // but observed funding is high enough to activate.
    auto signal = engine.on_market(
        MakeMarket(1000, true, true, 100.02, 0.0003, 1000));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, ObservedNegativeFundingCanBlockEntryEvenWithPositiveBasisProxy)
{
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.00005, 0.0, 1.0, 1.0, 0 });

    // Positive basis would imply positive proxy,
    // but observed funding is negative and should keep signal inactive.
    auto signal = engine.on_market(
        MakeMarket(1000, true, true, 100.5, -0.0001, 1000));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, HardNegativeFundingForcesExitEvenBeforeMinHold)
{
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, -0.00005, 1.0, 1.0, 0, 86'400'000 });

    // Enter with positive observed funding.
    auto s1 = engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);

    // Funding turns sufficiently negative before min_hold expires -> immediate exit.
    auto s2 = engine.on_market(MakeMarket(2000, true, true, 100.5, -0.0001, 2000));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, ExitRequiresFundingPersistenceAcrossSettlements)
{
    // Funding gate enabled, exit persistence = 2 settlements.
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.00001, 0.0, -1.0, 1.0, 1.0, 0, 0, 1, 2, true });

    // Enter on positive settlement snapshot.
    auto s1 = engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);

    // First negative settlement should not exit yet (needs 2 consecutive).
    auto s2 = engine.on_market(MakeMarket(2000, true, true, 100.5, -0.0001, 2000));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Active);

    // Second consecutive negative settlement triggers exit.
    auto s3 = engine.on_market(MakeMarket(3000, true, true, 100.5, -0.0002, 3000));
    EXPECT_EQ(s3.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, FallsBackToBasisProxyWhenObservedFundingIsStale)
{
    QTrading::Signal::FundingCarrySignalEngine engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.00005, 0.0, -1.0, 1.0, 1.0, 0, 0, 1, 1, true, 3600 });

    // Funding timestamp is far older than max age -> stale.
    // Basis proxy from 100.5/100.0 is positive and should allow entry.
    auto signal = engine.on_market(MakeMarket(10'000, true, true, 100.5, -0.0005, 0));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, FundingNowcastCanEnableEntryBeforeNextSettlement)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00018,   // entry threshold
        0.00010,   // exit threshold
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_confidence_enabled = false;
    cfg.funding_nowcast_enabled = true;
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.funding_nowcast_use_for_gates = true;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Settlement snapshot at t=0: observed funding=0.00010 (below entry threshold).
    auto s0 = engine.on_market(MakeMarket(0, true, true, 100.04, 0.00010, 0));
    EXPECT_EQ(s0.status, QTrading::Signal::SignalStatus::Inactive);

    // 2h into cycle: basis proxy implies target ~0.00030, linear nowcast -> ~0.00015 (still below threshold).
    auto s1 = engine.on_market(MakeMarket(2ull * 60ull * 60ull * 1000ull, true, true, 100.12, 0.00010, 0));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Inactive);

    // 4h into cycle: linear nowcast reaches ~0.00020 and should pass entry threshold.
    auto s2 = engine.on_market(MakeMarket(4ull * 60ull * 60ull * 1000ull, true, true, 100.12, 0.00010, 0));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, EntryGateCanUseNowcastWithoutApplyingNowcastToExitGate)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00018,   // entry threshold
        0.00005,   // exit threshold
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_confidence_enabled = false;
    cfg.funding_nowcast_enabled = true;
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.funding_nowcast_use_for_gates = false;
    cfg.funding_nowcast_use_for_entry_gate = true;
    cfg.funding_nowcast_use_for_exit_gate = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Settlement snapshot at t=0 below entry threshold.
    auto s0 = engine.on_market(MakeMarket(0, true, true, 100.04, 0.00010, 0));
    EXPECT_EQ(s0.status, QTrading::Signal::SignalStatus::Inactive);

    // 4h nowcast crosses entry threshold -> should enter.
    auto s1 = engine.on_market(MakeMarket(4ull * 60ull * 60ull * 1000ull, true, true, 100.12, 0.00010, 0));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);

    // Mid-cycle adverse basis would imply negative nowcast, but exit gate is settlement-based,
    // so no early exit before next settlement snapshot.
    auto s2 = engine.on_market(MakeMarket(6ull * 60ull * 60ull * 1000ull, true, true, 99.8, 0.00010, 0));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Active);

    // At settlement update, observed negative funding should trigger exit.
    auto s3 = engine.on_market(MakeMarket(8ull * 60ull * 60ull * 1000ull, true, true, 99.8, -0.00010, 8ull * 60ull * 60ull * 1000ull));
    EXPECT_EQ(s3.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, NowcastGateSamplingIntervalPreventsSubHourlyStreakUpdates)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00018,
        0.00005,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_confidence_enabled = false;
    cfg.funding_nowcast_enabled = true;
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.funding_nowcast_use_for_gates = false;
    cfg.funding_nowcast_use_for_entry_gate = true;
    cfg.funding_nowcast_use_for_exit_gate = false;
    cfg.funding_nowcast_gate_sample_ms = 60ull * 60ull * 1000ull; // 1h sample cadence.

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Settlement snapshot at t=0: below entry threshold.
    auto s0 = engine.on_market(MakeMarket(0, true, true, 100.04, 0.00010, 0));
    EXPECT_EQ(s0.status, QTrading::Signal::SignalStatus::Inactive);

    // 10min nowcast already above threshold (large basis), but sampling cadence blocks streak update.
    auto s1 = engine.on_market(MakeMarket(10ull * 60ull * 1000ull, true, true, 102.0, 0.00010, 0));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Inactive);

    // 1h sample boundary reached: now streak updates and entry is allowed.
    auto s2 = engine.on_market(MakeMarket(60ull * 60ull * 1000ull, true, true, 102.0, 0.00010, 0));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, ConfidencePathCanIgnoreNowcastWhenDisabled)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.0,
        0.0,
        -1.0,
        1.0,
        1.0,
        0
    };
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_structure_enabled = false;
    cfg.funding_nowcast_enabled = true;
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.funding_nowcast_use_for_confidence = false;

    auto cfg_nowcast_conf = cfg;
    cfg_nowcast_conf.funding_nowcast_use_for_confidence = true;

    QTrading::Signal::FundingCarrySignalEngine settle_conf_engine(cfg);
    QTrading::Signal::FundingCarrySignalEngine nowcast_conf_engine(cfg_nowcast_conf);

    const auto t0 = 0ull;
    const auto t4h = 4ull * 60ull * 60ull * 1000ull;

    // Settlement is negative, but basis-proxy is strongly positive.
    // If confidence path uses nowcast, confidence will rise mid-cycle.
    settle_conf_engine.on_market(MakeMarket(t0, true, true, 102.0, -0.0001, t0));
    nowcast_conf_engine.on_market(MakeMarket(t0, true, true, 102.0, -0.0001, t0));

    const auto settle_conf = settle_conf_engine.on_market(MakeMarket(t4h, true, true, 102.0, -0.0001, t0));
    const auto nowcast_conf = nowcast_conf_engine.on_market(MakeMarket(t4h, true, true, 102.0, -0.0001, t0));

    EXPECT_EQ(settle_conf.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(nowcast_conf.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_LT(settle_conf.confidence, 0.05);
    EXPECT_GT(nowcast_conf.confidence, 0.20);
    EXPECT_GT(nowcast_conf.confidence, settle_conf.confidence);
}

TEST(FundingCarrySignalEngineTests, PreSettlementNegativeFundingCanForceEarlyExit)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00001,
        0.0,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.pre_settlement_negative_exit_enabled = true;
    cfg.pre_settlement_negative_exit_threshold = -0.0002;
    cfg.pre_settlement_negative_exit_lookahead_ms = 60ull * 60ull * 1000ull; // 1h
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_confidence_enabled = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Initial positive settlement enters carry.
    auto s0 = engine.on_market(MakeMarket(0, true, true, 100.5, 0.0001, 0));
    EXPECT_EQ(s0.status, QTrading::Signal::SignalStatus::Active);

    // 6h before next settlement: projected negative exists but outside lookahead window -> stay active.
    auto s1 = engine.on_market(MakeMarket(6ull * 60ull * 60ull * 1000ull, true, true, 99.9, 0.0001, 0));
    EXPECT_EQ(s1.status, QTrading::Signal::SignalStatus::Active);

    // 7.5h: within 1h lookahead and projected next funding is deeply negative -> early exit.
    auto s2 = engine.on_market(MakeMarket(7ull * 60ull * 60ull * 1000ull + 30ull * 60ull * 1000ull, true, true, 99.9, 0.0001, 0));
    EXPECT_EQ(s2.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, PreSettlementNegativeExitBlocksImmediateReEntryUntilSettlement)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00001,
        0.0,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.pre_settlement_negative_exit_enabled = true;
    cfg.pre_settlement_negative_exit_threshold = -0.0002;
    cfg.pre_settlement_negative_exit_lookahead_ms = 60ull * 60ull * 1000ull; // 1h
    cfg.pre_settlement_negative_exit_reentry_buffer_ms = 0;
    cfg.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_confidence_enabled = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Enter from initial positive settlement.
    EXPECT_EQ(
        engine.on_market(MakeMarket(0, true, true, 100.5, 0.0001, 0)).status,
        QTrading::Signal::SignalStatus::Active);

    // Exit near settlement due projected deep negative funding.
    EXPECT_EQ(
        engine.on_market(MakeMarket(7ull * 60ull * 60ull * 1000ull + 30ull * 60ull * 1000ull, true, true, 99.9, 0.0001, 0)).status,
        QTrading::Signal::SignalStatus::Inactive);

    // Before projected settlement boundary: must remain inactive (block re-entry).
    EXPECT_EQ(
        engine.on_market(MakeMarket(7ull * 60ull * 60ull * 1000ull + 40ull * 60ull * 1000ull, true, true, 100.5, 0.0001, 0)).status,
        QTrading::Signal::SignalStatus::Inactive);

    // After projected settlement boundary: re-entry may resume.
    EXPECT_EQ(
        engine.on_market(MakeMarket(8ull * 60ull * 60ull * 1000ull + 60ull * 1000ull, true, true, 100.5, 0.0001, 0)).status,
        QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, ConfidenceTracksObservedFundingWhenGateDisabled)
{
    // Funding gate disabled (entry/exit threshold are zero).
    QTrading::Signal::FundingCarrySignalEngine pos_engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, -1.0, 1.0, 1.0, 0 });
    QTrading::Signal::FundingCarrySignalEngine neg_engine(
        { "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, -1.0, 1.0, 1.0, 0 });

    const auto pos = pos_engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000));
    const auto neg = neg_engine.on_market(MakeMarket(1000, true, true, 100.5, -0.0002, 1000));

    EXPECT_EQ(pos.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(neg.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_GT(pos.confidence, neg.confidence);
}

TEST(FundingCarrySignalEngineTests, AdaptiveRegimeTrendingUsesSlowerExitPersistence)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.00005,
        0.0,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.adaptive_regime_enabled = true;
    cfg.adaptive_regime_min_samples = 4;
    cfg.adaptive_regime_sign_window_settlements = 8;
    cfg.adaptive_regime_sign_persist_high = 0.9;
    cfg.adaptive_regime_sign_persist_low = 0.6;
    cfg.adaptive_regime_exit_persistence_low = 3;
    cfg.adaptive_regime_exit_persistence_mid = 2;
    cfg.adaptive_regime_exit_persistence_high = 1;
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_gate_enabled = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Build trending-positive settlement history so adaptive regime becomes "trending".
    EXPECT_EQ(
        engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(2000, true, true, 100.5, 0.0002, 2000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(3000, true, true, 100.5, 0.0002, 3000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(4000, true, true, 100.5, 0.0002, 4000)).status,
        QTrading::Signal::SignalStatus::Active);

    // After trend weakens, regime adapts and exit persistence becomes slower than baseline 1.
    EXPECT_EQ(
        engine.on_market(MakeMarket(5000, true, true, 100.5, -0.0001, 5000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(6000, true, true, 100.5, -0.0001, 6000)).status,
        QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, HardNegativeFundingExitCanRequireConsecutiveSettlements)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.0,
        0.0,
        -0.00005,
        1.0,
        1.0,
        0,
        86'400'000
    };
    cfg.hard_negative_persistence_settlements = 2;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    EXPECT_EQ(
        engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(2000, true, true, 100.5, -0.0001, 2000)).status,
        QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(
        engine.on_market(MakeMarket(3000, true, true, 100.5, -0.0001, 3000)).status,
        QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, InactivityWatchdogCanReEnterSmallPositiveFundingRegime)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.0002,
        0.0001,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.inactivity_watchdog_settlements = 2;
    cfg.inactivity_watchdog_min_rate = 0.0;
    cfg.inactivity_watchdog_min_confidence = 0.35;
    cfg.adaptive_confidence_enabled = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    // Below static threshold, should stay inactive initially.
    EXPECT_EQ(
        engine.on_market(MakeMarket(1000, true, true, 100.5, 0.00001, 1000)).status,
        QTrading::Signal::SignalStatus::Inactive);
    EXPECT_EQ(
        engine.on_market(MakeMarket(2000, true, true, 100.5, 0.00001, 2000)).status,
        QTrading::Signal::SignalStatus::Inactive);

    // Third inactive settlement should trigger watchdog entry.
    auto s3 = engine.on_market(MakeMarket(3000, true, true, 100.5, 0.00001, 3000));
    EXPECT_EQ(s3.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_GE(s3.confidence, 0.35);
}

TEST(FundingCarrySignalEngineTests, SoftFundingGateCanOverrideOverlyStrictStaticThreshold)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.0002,
        0.0001,
        -1.0,
        1.0,
        1.0,
        0,
        0,
        1,
        1,
        true
    };
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_funding_soft_gate_enabled = true;
    cfg.adaptive_funding_window_settlements = 8;
    cfg.adaptive_funding_min_samples = 3;
    cfg.adaptive_funding_entry_quantile = 0.60;
    cfg.adaptive_funding_exit_quantile = 0.40;
    cfg.adaptive_funding_entry_floor_rate = 0.00001;
    cfg.adaptive_funding_entry_cap_rate = 0.00008;
    cfg.adaptive_funding_exit_ratio = 0.30;
    cfg.adaptive_confidence_enabled = false;

    QTrading::Signal::FundingCarrySignalEngine engine(cfg);

    EXPECT_EQ(
        engine.on_market(MakeMarket(1000, true, true, 100.5, 0.00003, 1000)).status,
        QTrading::Signal::SignalStatus::Inactive);
    EXPECT_EQ(
        engine.on_market(MakeMarket(2000, true, true, 100.5, 0.00003, 2000)).status,
        QTrading::Signal::SignalStatus::Inactive);

    // Once enough settlements are observed, soft gate clamps threshold near this regime and allows entry.
    EXPECT_EQ(
        engine.on_market(MakeMarket(3000, true, true, 100.5, 0.00003, 3000)).status,
        QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, AdaptiveStructurePenalizesConfidenceInHostileFundingHistory)
{
    QTrading::Signal::FundingCarrySignalEngine::Config cfg{
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        0.0,
        0.0,
        -1.0,
        1.0,
        1.0,
        0
    };
    cfg.adaptive_confidence_enabled = false;
    cfg.adaptive_regime_enabled = false;
    cfg.adaptive_gate_enabled = false;
    cfg.adaptive_structure_enabled = true;
    cfg.adaptive_structure_min_samples = 4;
    cfg.adaptive_structure_neg_share_weight = 0.9;
    cfg.adaptive_structure_neg_run_weight = 0.6;
    cfg.adaptive_structure_neg_run_norm = 4.0;
    cfg.adaptive_structure_floor = 0.2;
    cfg.adaptive_structure_ceiling = 1.0;
    cfg.adaptive_structure_ema_alpha = 1.0;
    cfg.adaptive_structure_bucket_step = 0.0;

    QTrading::Signal::FundingCarrySignalEngine clean_engine(cfg);
    QTrading::Signal::FundingCarrySignalEngine hostile_engine(cfg);

    // Clean engine: mostly positive settlement history.
    clean_engine.on_market(MakeMarket(1000, true, true, 100.5, 0.0002, 1000));
    clean_engine.on_market(MakeMarket(2000, true, true, 100.5, 0.0002, 2000));
    clean_engine.on_market(MakeMarket(3000, true, true, 100.5, 0.0002, 3000));
    clean_engine.on_market(MakeMarket(4000, true, true, 100.5, 0.0002, 4000));

    // Hostile engine: same absolute level today, but recently dominated by negatives.
    hostile_engine.on_market(MakeMarket(1000, true, true, 100.5, -0.0002, 1000));
    hostile_engine.on_market(MakeMarket(2000, true, true, 100.5, -0.0002, 2000));
    hostile_engine.on_market(MakeMarket(3000, true, true, 100.5, -0.0002, 3000));
    hostile_engine.on_market(MakeMarket(4000, true, true, 100.5,  0.0002, 4000));

    const auto clean = clean_engine.on_market(MakeMarket(5000, true, true, 100.5, 0.0002, 5000));
    const auto hostile = hostile_engine.on_market(MakeMarket(5000, true, true, 100.5, 0.0002, 5000));

    EXPECT_EQ(clean.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_EQ(hostile.status, QTrading::Signal::SignalStatus::Active);
    EXPECT_GT(clean.confidence, hostile.confidence);
}
