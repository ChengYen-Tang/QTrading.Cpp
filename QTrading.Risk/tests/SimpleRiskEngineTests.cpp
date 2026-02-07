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

} // namespace

TEST(SimpleRiskEngineTests, ProducesTargetsForIntentLegs)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
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
    EXPECT_EQ(out.leverage["BTCUSDT_SPOT"], 2.0);
    EXPECT_EQ(out.leverage["BTCUSDT_PERP"], 2.0);
}

TEST(SimpleRiskEngineTests, FlattensExistingPositionsWhenNoIntent)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 1000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
    QTrading::Risk::SimpleRiskEngine engine(cfg);
    QTrading::Intent::TradeIntent intent;
    QTrading::Risk::AccountState account;

    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    account.positions.push_back(pos);

    auto out = engine.position(intent, account, nullptr);
    EXPECT_EQ(out.target_positions["BTCUSDT_PERP"], 0.0);
}

TEST(SimpleRiskEngineTests, CapsPerLegNotionalByConfig)
{
    QTrading::Risk::SimpleRiskEngine::Config cfg;
    cfg.notional_usdt = 500000.0;
    cfg.max_leg_notional_usdt = 200000.0;
    cfg.leverage = 2.0;
    cfg.max_leverage = 3.0;
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
