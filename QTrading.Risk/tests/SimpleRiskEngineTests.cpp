#include "Risk/SimpleRiskEngine.hpp"

#include <gtest/gtest.h>
#include <optional>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeTwoLegMarket(
    unsigned long long ts,
    double spot_close,
    double perp_close,
    std::optional<double> perp_mark = std::nullopt,
    std::optional<double> perp_index = std::nullopt)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back("BTCUSDT_SPOT");
    symbols->push_back("BTCUSDT_PERP");
    dto->symbols = symbols;
    dto->trade_klines_by_id.resize(2);
    dto->mark_klines_by_id.resize(2);
    dto->index_klines_by_id.resize(2);
    dto->trade_klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, spot_close, 0, ts, 0, 0, 0, 0);
    dto->trade_klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
    if (perp_mark.has_value()) {
        dto->mark_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, *perp_mark);
    }
    if (perp_index.has_value()) {
        dto->index_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, *perp_index);
    }
    return dto;
}

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeTwoLegMarketWithPerpFunding(
    unsigned long long ts,
    double spot_close,
    double perp_close,
    unsigned long long funding_time,
    double perp_funding_rate)
{
    auto dto = MakeTwoLegMarket(ts, spot_close, perp_close);
    dto->funding_by_id.resize(2);
    dto->funding_by_id[1] = QTrading::Dto::Market::Binance::FundingRateDto(
        funding_time,
        perp_funding_rate);
    return dto;
}

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeTwoLegMarketCustom(
    unsigned long long ts,
    const std::string& leg_a_symbol,
    double leg_a_close,
    const std::string& leg_b_symbol,
    double leg_b_close)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(leg_a_symbol);
    symbols->push_back(leg_b_symbol);
    dto->symbols = symbols;
    dto->trade_klines_by_id.resize(2);
    dto->trade_klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, leg_a_close, 0, ts, 0, 0, 0, 0);
    dto->trade_klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, leg_b_close, 0, ts, 0, 0, 0, 0);
    return dto;
}

void ApplyDefaultSpotPerpTypes(QTrading::Risk::SimpleRiskEngine::Config& cfg)
{
    cfg.instrument_types["BTCUSDT_SPOT"] = QTrading::Dto::Trading::InstrumentType::Spot;
    cfg.instrument_types["BTCUSDT_PERP"] = QTrading::Dto::Trading::InstrumentType::Perp;
}

} // namespace

TEST(SimpleRiskEngineTests, ProducesTargetsForIntentLegs)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);
    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_zscore";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto out = engine.position(intent, account, nullptr);

    EXPECT_EQ(out.target_positions["BTCUSDT_SPOT"], 1000.0);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], -1000.0);
    EXPECT_EQ(out.leverage["BTCUSDT_SPOT"], 1.0);
    EXPECT_EQ(out.leverage["BTCUSDT_PERP"], 2.0);
}

TEST(SimpleRiskEngineTests, FlattensExistingPositionsWhenNoIntent)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);
    QTrading::Intent::TradeIntent intent;
    QTrading::Risk::AccountState account;

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    account.positions.push_back(pos);

    auto out = engine.position(intent, account, nullptr);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], 0.0);
}

TEST(SimpleRiskEngineTests, FlattensSpotWithOneXLeverageWhenNoIntent)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);
    QTrading::Intent::TradeIntent intent;
    QTrading::Risk::AccountState account;

    QTrading::dto::Position spot{};
    spot.symbol = "BTCUSDT_SPOT";
    account.positions.push_back(spot);

    auto out = engine.position(intent, account, nullptr);
    EXPECT_EQ(out.target_positions["BTCUSDT_SPOT"], 0.0);
    EXPECT_EQ(out.leverage["BTCUSDT_SPOT"], 1.0);
}

TEST(SimpleRiskEngineTests, FlattensTypedPositionsWithNoIntent)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 4.0;
    cfg.max_leverage = 5.0;
    ApplyDefaultSpotPerpTypes(cfg);
    cfg.instrument_types["BTCUSDT_CASH"] = QTrading::Dto::Trading::InstrumentType::Spot;
    cfg.instrument_types["BTCUSDT_SWAP"] = QTrading::Dto::Trading::InstrumentType::Perp;
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    QTrading::Risk::AccountState account;

    QTrading::dto::Position spot{};
    spot.symbol = "BTCUSDT_CASH";
    spot.quantity = 1.0;
    spot.is_long = true;
    account.positions.push_back(spot);

    QTrading::dto::Position perp{};
    perp.symbol = "BTCUSDT_SWAP";
    perp.quantity = 1.0;
    perp.is_long = false;
    account.positions.push_back(perp);

    auto out = engine.position(intent, account, nullptr);
    EXPECT_EQ(out.target_positions["BTCUSDT_CASH"], 0.0);
    EXPECT_EQ(out.target_positions["BTCUSDT_SWAP"], 0.0);
    EXPECT_EQ(out.leverage["BTCUSDT_CASH"], 1.0);
    EXPECT_EQ(out.leverage["BTCUSDT_SWAP"], 4.0);
}

TEST(SimpleRiskEngineTests, UsesExplicitInstrumentTypesWithoutSuffixNames)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    cfg.instrument_types["BTCUSDT_CASH"] = QTrading::Dto::Trading::InstrumentType::Spot;
    cfg.instrument_types["BTCUSDT_SWAP"] = QTrading::Dto::Trading::InstrumentType::Perp;
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_CASH", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_SWAP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarketCustom(1, "BTCUSDT_CASH", 100.0, "BTCUSDT_SWAP", 101.0);
    auto out = engine.position(intent, account, market);

    EXPECT_EQ(out.target_positions["BTCUSDT_CASH"], 1000.0);
    EXPECT_EQ(out.target_positions["BTCUSDT_SWAP"], -1010.0);
    EXPECT_EQ(out.leverage["BTCUSDT_CASH"], 1.0);
    EXPECT_EQ(out.leverage["BTCUSDT_SWAP"], 2.0);
}

TEST(SimpleRiskEngineTests, BasisArbitrageUsesLinearDeltaNeutralSizing)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1010.0, 1e-9);
    EXPECT_EQ(out.leverage["BTCUSDT_SPOT"], 1.0);
    EXPECT_EQ(out.leverage["BTCUSDT_PERP"], 2.0);
}

TEST(SimpleRiskEngineTests, MarkIndexSoftDeriskScalesCarryTargetNotional)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.mark_index_soft_derisk_start_bps = 50.0;
    cfg.mark_index_soft_derisk_full_bps = 150.0;
    cfg.mark_index_soft_derisk_min_scale = 0.4;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto calm_market = MakeTwoLegMarket(1, 100.0, 101.0, 100.1, 100.0);     // 10 bps
    auto stressed_market = MakeTwoLegMarket(2, 100.0, 101.0, 101.5, 100.0); // 150 bps

    const auto calm = engine.position(intent, account, calm_market);
    intent.ts_ms = 2;
    const auto stressed = engine.position(intent, account, stressed_market);

    EXPECT_NEAR(calm.target_positions.at("BTCUSDT_SPOT"), 1000.0, 1e-9);
    EXPECT_NEAR(stressed.target_positions.at("BTCUSDT_SPOT"), 400.0, 1e-9);
    EXPECT_NEAR(stressed.target_positions.at("BTCUSDT_PERP"), -404.0, 1e-9);
}

TEST(SimpleRiskEngineTests, MarkIndexHardGuardFlattensCarryTargets)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.mark_index_hard_guard_bps = 120.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto hard_market = MakeTwoLegMarket(1, 100.0, 101.0, 101.3, 100.0); // 130 bps
    const auto out = engine.position(intent, account, hard_market);

    EXPECT_NEAR(out.target_positions.at("BTCUSDT_SPOT"), 0.0, 1e-9);
    EXPECT_NEAR(out.target_positions.at("BTCUSDT_PERP"), 0.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisAlphaOverlayScalesBasisArbitrageNotionalByDirectionalBasis)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.basis_alpha_overlay_enabled = true;
    cfg.basis_alpha_overlay_center_pct = 0.0;
    cfg.basis_alpha_overlay_band_pct = 0.01;
    cfg.basis_alpha_overlay_upscale_cap = 1.20;
    cfg.basis_alpha_overlay_downscale_floor = 0.80;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    // Positive basis (+1%) => favorable for short-perp/long-spot -> upscale to cap.
    auto positive_basis = MakeTwoLegMarket(1, 100.0, 101.0);
    const auto out_pos = engine.position(intent, account, positive_basis);
    EXPECT_NEAR(out_pos.target_positions.at("BTCUSDT_SPOT"), 1200.0, 1e-9);
    EXPECT_NEAR(out_pos.target_positions.at("BTCUSDT_PERP"), -1212.0, 1e-9);

    // Negative basis (-1%) => adverse direction -> downscale to floor.
    intent.ts_ms = 2;
    auto negative_basis = MakeTwoLegMarket(2, 100.0, 99.0);
    const auto out_neg = engine.position(intent, account, negative_basis);
    EXPECT_NEAR(out_neg.target_positions.at("BTCUSDT_SPOT"), 800.0, 1e-9);
    EXPECT_NEAR(out_neg.target_positions.at("BTCUSDT_PERP"), -792.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisArbitrageUsesConfidenceSizedNotionalAndLeverage)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_min_scale = 0.2;
    cfg.carry_confidence_min_leverage_scale = 0.5;
    cfg.carry_confidence_max_leverage_scale = 1.5;
    cfg.carry_confidence_power = 1.0;
    cfg.carry_confidence_leverage_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.confidence = 0.25;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // confidence scale = 0.2 + 0.8 * 0.25 = 0.4
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 400.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -404.0, 1e-9);
    // leverage scale = 0.5 + (1.5 - 0.5) * 0.25 = 0.75 -> perp leverage = 2.0 * 0.75 = 1.5
    EXPECT_NEAR(out.leverage["BTCUSDT_PERP"], 1.5, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisArbitrageIgnoresCarryFundingEconomicsGate)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_size_cost_rate_per_leg = 0.0003;
    cfg.carry_size_expected_hold_settlements = 2.0;
    cfg.carry_size_min_gain_to_cost_low_confidence = 2.5;
    cfg.carry_size_min_gain_to_cost_high_confidence = 1.2;
    cfg.carry_size_gain_to_cost_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.confidence = 0.10;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarketWithPerpFunding(1, 100.0, 101.0, 1, 0.0003);
    auto out = engine.position(intent, account, market);

    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1010.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisArbitrageLeverageAllocatorUsesCashAndConfidence)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 100000000.0;
    cfg.leverage = 3.0;
    cfg.max_leverage = 4.0;
    cfg.dual_ledger_auto_notional_ratio = 0.0;
    cfg.carry_allocator_leverage_model_enabled = true;
    cfg.carry_allocator_spot_cash_per_notional = 1.0;
    cfg.carry_allocator_perp_margin_buffer_ratio = 0.08;
    cfg.carry_allocator_perp_leverage = 4.0;
    cfg.carry_confidence_min_scale = 0.2;
    cfg.carry_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_arbitrage";
    intent.structure = "delta_neutral_basis";
    intent.ts_ms = 1;
    intent.confidence = 0.25;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.AvailableBalance = 100'000'000.0;
    spot.WalletBalance = 100'000'000.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.AvailableBalance = 100'000'000.0;
    perp.WalletBalance = 100'000'000.0;
    perp.Equity = 100'000'000.0;
    account.perp_balance = perp;
    account.total_cash_balance = 100'000'000.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // allocator target = 100,000,000 / (1 + 0.25 + 0.08) = 75,187,969.924812...
    // confidence scale = 0.2 + 0.8 * 0.25 = 0.4
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 30075187.96992481, 1e-2);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -30375939.84962406, 1e-2);
}

TEST(SimpleRiskEngineTests, CapsPerLegNotionalByConfig)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 500000.0;
    cfg.max_leg_notional_usdt = 200000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_zscore";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto out = engine.position(intent, account, nullptr);

    EXPECT_EQ(out.target_positions["BTCUSDT_SPOT"], 200000.0);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], -200000.0);
}

TEST(SimpleRiskEngineTests, GrossDeviationTriggerRebalancesWhenUnderExposedButNetNeutral)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 200000.0;
    cfg.max_leg_notional_usdt = 200000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.rebalance_threshold_ratio = 0.01;
    cfg.gross_deviation_trigger_ratio = 0.30;
    cfg.gross_deviation_trigger_notional_threshold = 50000.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    QTrading::dto::Position spot{};
    spot.symbol = "BTCUSDT_SPOT";
    spot.quantity = 1000.0; // Notional 100,000 at price 100
    spot.is_long = true;
    account.positions.push_back(spot);

    QTrading::dto::Position perp{};
    perp.symbol = "BTCUSDT_PERP";
    perp.quantity = 1000.0; // Notional -100,000 at price 100
    perp.is_long = false;
    account.positions.push_back(perp);

    auto market = MakeTwoLegMarket(1, 100.0, 100.0);
    auto out = engine.position(intent, account, market);

    // Net is neutral, but gross is only half of target (200k vs 400k), so trigger should rebalance.
    EXPECT_DOUBLE_EQ(out.target_positions["BTCUSDT_SPOT"], 200000.0);
    EXPECT_DOUBLE_EQ(out.target_positions["BTCUSDT_PERP"], -200000.0);
}

TEST(SimpleRiskEngineTests, NegBasisScaleHysteresisAvoidsRapidFlipNearThreshold)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.neg_basis_threshold = 0.0;
    cfg.neg_basis_scale = 0.8;
    cfg.neg_basis_hysteresis_pct = 0.001; // 10 bps band around threshold.
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    // Enter scale-active mode: basis = -0.2%
    auto m1 = MakeTwoLegMarket(1, 100.0, 99.8);
    auto out1 = engine.position(intent, account, m1);
    EXPECT_NEAR(out1.target_positions["BTCUSDT_SPOT"], 800.0, 1e-9);

    // Move inside hysteresis band (+0.05%): should remain scale-active.
    auto m2 = MakeTwoLegMarket(2, 100.0, 100.05);
    auto out2 = engine.position(intent, account, m2);
    EXPECT_NEAR(out2.target_positions["BTCUSDT_SPOT"], 800.0, 1e-9);

    // Exit above upper band (+0.15%): scale should deactivate.
    auto m3 = MakeTwoLegMarket(3, 100.0, 100.15);
    auto out3 = engine.position(intent, account, m3);
    EXPECT_NEAR(out3.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisOverlayDoesNotUpscaleWhenUpscaleDisabled)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.basis_overlay_cap = 0.01;
    cfg.basis_overlay_strength = 0.50;
    cfg.basis_overlay_allow_upscale = false;
    cfg.basis_overlay_upscale_cap = 1.20;
    cfg.basis_overlay_downscale_floor = 0.50;
    cfg.basis_overlay_refresh_ms = 0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    // Warm up basis EMA around 0%.
    auto warm = MakeTwoLegMarket(1, 100.0, 100.0);
    (void)engine.position(intent, account, warm);

    // Positive basis deviation would imply overlay > 1.0.
    // With upscale disabled, target should stay at base notional.
    auto rich_basis = MakeTwoLegMarket(2, 100.0, 101.0);
    auto out = engine.position(intent, account, rich_basis);
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1010.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisOverlayUpscalesWhenEnabledAndClampedByCap)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 2000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.basis_overlay_cap = 0.01;
    cfg.basis_overlay_strength = 0.50;
    cfg.basis_overlay_allow_upscale = true;
    cfg.basis_overlay_upscale_cap = 1.20;
    cfg.basis_overlay_downscale_floor = 0.50;
    cfg.basis_overlay_refresh_ms = 0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    // Warm up basis EMA around 0%.
    auto warm = MakeTwoLegMarket(1, 100.0, 100.0);
    (void)engine.position(intent, account, warm);

    // Positive basis deviation increases target scale, but should be clipped by upscale cap (1.20).
    auto rich_basis = MakeTwoLegMarket(2, 100.0, 101.0);
    auto out = engine.position(intent, account, rich_basis);
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1200.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1212.0, 1e-9);
}

TEST(SimpleRiskEngineTests, BasisOverlayRefreshIntervalPreventsMinuteLevelTargetJitter)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 2000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.basis_overlay_cap = 0.01;
    cfg.basis_overlay_strength = 0.50;
    cfg.basis_overlay_allow_upscale = true;
    cfg.basis_overlay_upscale_cap = 1.20;
    cfg.basis_overlay_downscale_floor = 0.50;
    cfg.basis_overlay_activation_ratio = 0.0;
    cfg.basis_overlay_refresh_ms = 1000;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    auto warm = MakeTwoLegMarket(1000, 100.0, 100.0);
    auto out_warm = engine.position(intent, account, warm);
    EXPECT_NEAR(out_warm.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);

    // Within refresh interval: overlay should stay at previous multiplier (1.0).
    auto rich_before_refresh = MakeTwoLegMarket(1500, 100.0, 101.0);
    auto out_before_refresh = engine.position(intent, account, rich_before_refresh);
    EXPECT_NEAR(out_before_refresh.target_positions["BTCUSDT_SPOT"], 1000.0, 1e-9);

    // After refresh interval: overlay can update and upscale target.
    auto rich_after_refresh = MakeTwoLegMarket(2001, 100.0, 101.0);
    auto out_after_refresh = engine.position(intent, account, rich_after_refresh);
    EXPECT_NEAR(out_after_refresh.target_positions["BTCUSDT_SPOT"], 1200.0, 1e-9);
    EXPECT_NEAR(out_after_refresh.target_positions["BTCUSDT_PERP"], -1212.0, 1e-9);
}

TEST(SimpleRiskEngineTests, DualLedgerSnapshotsDoNotChangeSizingWhenUnused)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "basis_zscore";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState without_dual_ledger;
    auto out_without = engine.position(intent, without_dual_ledger, nullptr);

    QTrading::Risk::AccountState with_dual_ledger;
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.WalletBalance = 500'000.0;
    spot.MarginBalance = 500'000.0;
    spot.AvailableBalance = 500'000.0;
    spot.Equity = 500'000.0;
    with_dual_ledger.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.WalletBalance = 500'000.0;
    perp.MarginBalance = 500'000.0;
    perp.AvailableBalance = 500'000.0;
    perp.Equity = 500'000.0;
    with_dual_ledger.perp_balance = perp;
    with_dual_ledger.total_cash_balance = 1'000'000.0;
    auto out_with = engine.position(intent, with_dual_ledger, nullptr);

    EXPECT_DOUBLE_EQ(out_without.target_positions["BTCUSDT_SPOT"], out_with.target_positions["BTCUSDT_SPOT"]);
    EXPECT_DOUBLE_EQ(out_without.target_positions["BTCUSDT_PERP"], out_with.target_positions["BTCUSDT_PERP"]);
    EXPECT_DOUBLE_EQ(out_without.leverage["BTCUSDT_SPOT"], out_with.leverage["BTCUSDT_SPOT"]);
    EXPECT_DOUBLE_EQ(out_without.leverage["BTCUSDT_PERP"], out_with.leverage["BTCUSDT_PERP"]);
}

TEST(SimpleRiskEngineTests, DualLedgerSnapshotsKeepCarryRebalancePathStable)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.rebalance_threshold_ratio = 0.01;
    // This test validates net-neutral early-return stability only.
    // Disable gross-deviation override so auto-notional does not force rebalance.
    cfg.gross_deviation_trigger_notional_threshold = std::numeric_limits<double>::infinity();
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::dto::Position spot{};
    spot.symbol = "BTCUSDT_SPOT";
    spot.quantity = 1.0;
    spot.is_long = true;
    account.positions.push_back(spot);

    QTrading::dto::Position perp{};
    perp.symbol = "BTCUSDT_PERP";
    perp.quantity = 1.0;
    perp.is_long = false;
    account.positions.push_back(perp);

    QTrading::Dto::Account::BalanceSnapshot spot_bal{};
    spot_bal.WalletBalance = 500'000.0;
    spot_bal.MarginBalance = 500'000.0;
    spot_bal.AvailableBalance = 500'000.0;
    spot_bal.Equity = 500'000.0;
    account.spot_balance = spot_bal;

    QTrading::Dto::Account::BalanceSnapshot perp_bal{};
    perp_bal.WalletBalance = 50'000.0;
    perp_bal.MarginBalance = 50'000.0;
    perp_bal.AvailableBalance = 40'000.0;
    perp_bal.Equity = 50'000.0;
    account.perp_balance = perp_bal;
    account.total_cash_balance = 550'000.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // In carry path with net/gross under threshold, keep current notionals.
    EXPECT_DOUBLE_EQ(out.target_positions["BTCUSDT_SPOT"], 100.0);
    EXPECT_DOUBLE_EQ(out.target_positions["BTCUSDT_PERP"], -101.0);
    EXPECT_DOUBLE_EQ(out.leverage["BTCUSDT_SPOT"], 1.0);
    EXPECT_DOUBLE_EQ(out.leverage["BTCUSDT_PERP"], 2.0);
}

TEST(SimpleRiskEngineTests, CarryDualLedgerAutoNotionalUsesTotalCashRatio)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 200000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.dual_ledger_auto_notional_ratio = 0.10;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.AvailableBalance = 500'000.0;
    spot.WalletBalance = 500'000.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.AvailableBalance = 500'000.0;
    perp.WalletBalance = 500'000.0;
    perp.Equity = 500'000.0;
    account.perp_balance = perp;
    account.total_cash_balance = 1'000'000.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 100000.0, 1e-6);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -101000.0, 1e-6);
}

TEST(SimpleRiskEngineTests, CarryDualLedgerCapacityKeepsCurrentSpotExposureWhenCashIsLow)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 200000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.dual_ledger_auto_notional_ratio = 0.10;
    cfg.dual_ledger_spot_available_usage = 1.0;
    cfg.dual_ledger_perp_available_usage = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::dto::Position spot_pos{};
    spot_pos.symbol = "BTCUSDT_SPOT";
    spot_pos.quantity = 1000.0; // 100k spot notional at spot=100
    spot_pos.is_long = true;
    account.positions.push_back(spot_pos);

    QTrading::dto::Position perp_pos{};
    perp_pos.symbol = "BTCUSDT_PERP";
    perp_pos.quantity = 990.09900990099; // ~100k short notional at perp=101
    perp_pos.is_long = false;
    account.positions.push_back(perp_pos);

    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.AvailableBalance = 100.0; // little remaining spot cash
    spot.WalletBalance = 100.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.AvailableBalance = 10'000.0;
    perp.WalletBalance = 50'000.0;
    perp.Equity = 50'000.0;
    account.perp_balance = perp;
    account.total_cash_balance = 50'100.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // Spot capacity includes existing spot inventory notional, so target should not collapse to near zero.
    EXPECT_GT(out.target_positions["BTCUSDT_SPOT"], 99000.0);
    EXPECT_LT(out.target_positions["BTCUSDT_SPOT"], 100200.0);
    EXPECT_LT(out.target_positions["BTCUSDT_PERP"], -99900.0);
}

TEST(SimpleRiskEngineTests, CarryDualLedgerAutoNotionalEmaSmoothsTargetAfterCashDrop)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 400000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.dual_ledger_auto_notional_ratio = 0.40;
    cfg.dual_ledger_auto_notional_ema_alpha = 0.10;
    cfg.dual_ledger_spot_available_usage = 1.0;
    cfg.dual_ledger_perp_available_usage = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.WalletBalance = 1'000'000.0;
    spot.AvailableBalance = 1'000'000.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.WalletBalance = 1'000'000.0;
    perp.AvailableBalance = 1'000'000.0;
    perp.Equity = 1'000'000.0;
    account.perp_balance = perp;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);

    // First snapshot: auto target = 400,000 (40% of 1,000,000).
    account.total_cash_balance = 1'000'000.0;
    auto out1 = engine.position(intent, account, market);
    EXPECT_NEAR(out1.target_positions["BTCUSDT_SPOT"], 400000.0, 1e-6);

    // Cash drops to 500,000; with alpha=0.10, smoothed target is:
    // 0.9 * 400,000 + 0.1 * 200,000 = 380,000 (instead of an abrupt 200,000).
    intent.ts_ms = 2;
    account.total_cash_balance = 500'000.0;
    auto out2 = engine.position(intent, account, market);
    EXPECT_NEAR(out2.target_positions["BTCUSDT_SPOT"], 380000.0, 1e-6);
    EXPECT_NEAR(out2.target_positions["BTCUSDT_PERP"], -383800.0, 1e-3);
}

TEST(SimpleRiskEngineTests, CarryConfidenceScalesDownTargetNotional)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_min_scale = 0.2;
    cfg.carry_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 0.25;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // confidence scale = 0.2 + 0.8 * 0.25 = 0.4 -> target spot leg = 400 notional.
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 400.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -404.0, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryConfidenceCanScaleUpWithinLegCap)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1400.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_min_scale = 0.8;
    cfg.carry_confidence_max_scale = 1.5;
    cfg.carry_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 1.0;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // Raw scale would request 1,500, but max_leg_notional clamps each leg to 1,400.
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1400.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1414.0, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryConfidenceScalesPerpLeverage)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_min_leverage_scale = 0.5;
    cfg.carry_confidence_max_leverage_scale = 1.5;
    cfg.carry_confidence_leverage_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 0.25;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // leverage scale = 0.5 + (1.5 - 0.5) * 0.25 = 0.75 -> perp leverage = 2.0 * 0.75 = 1.5
    EXPECT_NEAR(out.leverage["BTCUSDT_SPOT"], 1.0, 1e-9);
    EXPECT_NEAR(out.leverage["BTCUSDT_PERP"], 1.5, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryCoreOverlayKeepsCoreAtLowConfidence)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_core_overlay_enabled = true;
    cfg.carry_core_notional_ratio = 0.70;
    cfg.carry_overlay_notional_ratio = 0.30;
    cfg.carry_overlay_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 0.0;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // model_scale = 0.70 + 0.30 * 0.0 = 0.70
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 700.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -707.0, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryConfidenceBoostAddsSizeOnlyAboveReference)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 2000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_boost_enabled = true;
    cfg.carry_confidence_boost_reference = 0.50;
    cfg.carry_confidence_boost_max_scale = 0.20;
    cfg.carry_confidence_boost_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 0.90;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;
    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // normalized = (0.90 - 0.50) / 0.50 = 0.80
    // boost_scale = 1 + 0.20 * 0.80 = 1.16
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 1160.0, 1e-9);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -1171.6, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryConfidenceBoostRegimeAwareDampsBoostUnderNegativeFundingShare)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 2000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_confidence_boost_enabled = true;
    cfg.carry_confidence_boost_reference = 0.50;
    cfg.carry_confidence_boost_max_scale = 0.20;
    cfg.carry_confidence_boost_power = 1.0;
    cfg.carry_confidence_boost_regime_aware_enabled = true;
    cfg.carry_confidence_boost_regime_window_settlements = 10;
    cfg.carry_confidence_boost_regime_min_samples = 2;
    cfg.carry_confidence_boost_regime_negative_share_weight = 1.0;
    cfg.carry_confidence_boost_regime_floor_scale = 0.50;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 0.90;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account;

    // First settlement sample: positive funding. Not enough samples for regime damping yet.
    auto m1 = MakeTwoLegMarketWithPerpFunding(1, 100.0, 101.0, 1, 0.0001);
    auto out1 = engine.position(intent, account, m1);
    EXPECT_NEAR(out1.target_positions["BTCUSDT_SPOT"], 1160.0, 1e-9);

    // Second settlement sample: negative funding. negative_share = 1/2 = 0.5.
    // regime_scale = max(0.5, 1 - 1.0 * 0.5) = 0.5
    // effective boost max = 0.20 * 0.5 = 0.10
    // normalized = (0.90 - 0.50) / 0.50 = 0.80
    // final boost scale = 1 + 0.10 * 0.80 = 1.08
    intent.ts_ms = 2;
    auto m2 = MakeTwoLegMarketWithPerpFunding(2, 100.0, 101.0, 2, -0.0001);
    auto out2 = engine.position(intent, account, m2);
    EXPECT_NEAR(out2.target_positions["BTCUSDT_SPOT"], 1080.0, 1e-9);
    EXPECT_NEAR(out2.target_positions["BTCUSDT_PERP"], -1090.8, 1e-9);
}

TEST(SimpleRiskEngineTests, PerpLiquidationBufferGuardScalesDownCarryNotional)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 400000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.dual_ledger_auto_notional_ratio = 0.40;
    cfg.perp_liq_buffer_floor_ratio = 0.50;
    cfg.perp_liq_buffer_ceiling_ratio = 1.50;
    cfg.perp_liq_min_notional_scale = 0.30;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.WalletBalance = 1'000'000.0;
    spot.AvailableBalance = 1'000'000.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.WalletBalance = 200'000.0;
    perp.MarginBalance = 120'000.0;
    perp.MaintenanceMargin = 100'000.0; // buffer_ratio = 0.2 -> below floor -> min scale.
    perp.AvailableBalance = 200'000.0;
    perp.Equity = 120'000.0;
    account.perp_balance = perp;
    account.total_cash_balance = 1'000'000.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // Auto target 400,000 scaled by liquidation guard min scale 0.30 -> 120,000.
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 120000.0, 1e-6);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -121200.0, 1e-3);
}

TEST(SimpleRiskEngineTests, CarrySizeEconomicsGateCanUseConfidenceAdaptiveThreshold)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.max_leg_notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    cfg.carry_size_cost_rate_per_leg = 0.0003;
    cfg.carry_size_expected_hold_settlements = 2.0;
    cfg.carry_size_min_gain_to_cost = 1.0;
    cfg.carry_size_min_gain_to_cost_low_confidence = 2.5;
    cfg.carry_size_min_gain_to_cost_high_confidence = 1.2;
    cfg.carry_size_gain_to_cost_confidence_power = 1.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    auto market = MakeTwoLegMarketWithPerpFunding(1, 100.0, 101.0, 1, 0.0003);
    QTrading::Risk::AccountState account;

    QTrading::Intent::TradeIntent low_conf_intent;
    low_conf_intent.strategy = "funding_carry";
    low_conf_intent.structure = "delta_neutral_carry";
    low_conf_intent.ts_ms = 1;
    low_conf_intent.confidence = 0.10;
    low_conf_intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    low_conf_intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    const auto low_conf_out = engine.position(low_conf_intent, account, market);
    // ratio(gain/cost) ~= 2.0, but low-confidence required ratio ~= 2.37 -> gate blocks size change.
    EXPECT_NEAR(low_conf_out.target_positions.at("BTCUSDT_SPOT"), 0.0, 1e-9);
    EXPECT_NEAR(low_conf_out.target_positions.at("BTCUSDT_PERP"), 0.0, 1e-9);

    QTrading::Intent::TradeIntent high_conf_intent = low_conf_intent;
    high_conf_intent.confidence = 0.90;
    high_conf_intent.ts_ms = 2;

    const auto high_conf_out = engine.position(high_conf_intent, account, market);
    // ratio(gain/cost) ~= 2.0, high-confidence required ratio ~= 1.33 -> size change allowed.
    EXPECT_NEAR(high_conf_out.target_positions.at("BTCUSDT_SPOT"), 1000.0, 1e-9);
    EXPECT_NEAR(high_conf_out.target_positions.at("BTCUSDT_PERP"), -1010.0, 1e-9);
}

TEST(SimpleRiskEngineTests, CarryLeverageAllocatorDerivesTargetFromCashAndLeverage)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 3.0;
    cfg.max_leverage = 4.0;
    cfg.dual_ledger_auto_notional_ratio = 0.0; // force allocator path
    cfg.carry_allocator_leverage_model_enabled = true;
    cfg.carry_allocator_spot_cash_per_notional = 1.0;
    cfg.carry_allocator_perp_margin_buffer_ratio = 0.08;
    cfg.carry_allocator_perp_leverage = 4.0;
    ApplyDefaultSpotPerpTypes(cfg);
    QTrading::Risk::SimpleRiskEngine engine(cfg);

    QTrading::Intent::TradeIntent intent;
    intent.strategy = "funding_carry";
    intent.structure = "delta_neutral_carry";
    intent.ts_ms = 1;
    intent.confidence = 1.0;
    intent.legs.push_back({ "BTCUSDT_SPOT", QTrading::Intent::TradeSide::Long });
    intent.legs.push_back({ "BTCUSDT_PERP", QTrading::Intent::TradeSide::Short });

    QTrading::Risk::AccountState account{};
    QTrading::Dto::Account::BalanceSnapshot spot{};
    spot.AvailableBalance = 100'000'000.0;
    spot.WalletBalance = 100'000'000.0;
    account.spot_balance = spot;

    QTrading::Dto::Account::BalanceSnapshot perp{};
    perp.AvailableBalance = 100'000'000.0;
    perp.WalletBalance = 100'000'000.0;
    perp.Equity = 100'000'000.0;
    account.perp_balance = perp;
    account.total_cash_balance = 100'000'000.0;

    auto market = MakeTwoLegMarket(1, 100.0, 101.0);
    auto out = engine.position(intent, account, market);

    // target_leg_notional = cash / (spot_cash + 1/lev + buffer)
    // = 100,000,000 / (1 + 0.25 + 0.08) = 75,187,969.924812...
    EXPECT_NEAR(out.target_positions["BTCUSDT_SPOT"], 75187969.92481203, 1e-2);
    EXPECT_NEAR(out.target_positions["BTCUSDT_PERP"], -75939849.62406015, 1e-2);
}
