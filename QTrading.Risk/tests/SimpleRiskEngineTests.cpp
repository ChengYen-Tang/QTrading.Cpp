#include "Risk/SimpleRiskEngine.hpp"

#include <gtest/gtest.h>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeTwoLegMarket(
    unsigned long long ts,
    double spot_close,
    double perp_close)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back("BTCUSDT_SPOT");
    symbols->push_back("BTCUSDT_PERP");
    dto->symbols = symbols;
    dto->klines_by_id.resize(2);
    dto->klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, spot_close, 0, ts, 0, 0, 0, 0);
    dto->klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
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
    dto->klines_by_id.resize(2);
    dto->klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, leg_a_close, 0, ts, 0, 0, 0, 0);
    dto->klines_by_id[1] = QTrading::Dto::Market::Binance::KlineDto(ts, 0, 0, 0, leg_b_close, 0, ts, 0, 0, 0, 0);
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
