#include "Intent/BasisArbitrageIntentBuilder.hpp"

#include <gtest/gtest.h>
#include <memory>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(unsigned long long ts)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    dto->symbols = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    dto->trade_klines_by_id.resize(2);
    dto->trade_klines_by_id[0] =
        QTrading::Dto::Market::Binance::TradeKlineDto(ts, 0, 0, 0, 100.0, 0, ts, 0, 0, 0, 0);
    dto->trade_klines_by_id[1] =
        QTrading::Dto::Market::Binance::TradeKlineDto(ts, 0, 0, 0, 101.0, 0, ts, 0, 0, 0, 0);
    dto->mark_klines_by_id.resize(2);
    dto->index_klines_by_id.resize(2);
    dto->mark_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, 101.0);
    dto->index_klines_by_id[1] = QTrading::Dto::Market::Binance::ReferenceKlineDto::Point(ts, 100.0);
    return dto;
}

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarketWithBasis(
    unsigned long long ts,
    double perp_price)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    dto->symbols = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    dto->trade_klines_by_id.resize(2);
    dto->trade_klines_by_id[0] =
        QTrading::Dto::Market::Binance::TradeKlineDto(ts, 0, 0, 0, 100.0, 0, ts, 0, 0, 0, 0);
    dto->trade_klines_by_id[1] =
        QTrading::Dto::Market::Binance::TradeKlineDto(ts, 0, 0, 0, perp_price, 0, ts, 0, 0, 0, 0);
    return dto;
}

} // namespace

TEST(BasisArbitrageIntentBuilderTests, UsesBasisStrategyMetadata)
{
    QTrading::Intent::BasisArbitrageIntentBuilder builder({});

    QTrading::Signal::SignalDecision signal{};
    signal.ts_ms = 1234;
    signal.strategy = "basis_arbitrage";
    signal.status = QTrading::Signal::SignalStatus::Active;
    signal.urgency = QTrading::Signal::SignalUrgency::Low;
    signal.confidence = 0.8;

    const auto intent = builder.build(signal, MakeMarket(1234));

    EXPECT_EQ(intent.strategy, "basis_arbitrage");
    EXPECT_EQ(intent.structure, "delta_neutral_basis");
    EXPECT_EQ(intent.reason, "basis_arbitrage");
    EXPECT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.intent_id.rfind("basis_arbitrage:", 0), 0u);
}

TEST(BasisArbitrageIntentBuilderTests, DirectionalModeSwitchesLegsByBasisSignWithThreshold)
{
    QTrading::Intent::BasisArbitrageIntentBuilder::Config cfg{};
    cfg.basis_directional_enabled = true;
    cfg.basis_direction_use_mark_index = false;
    cfg.basis_direction_switch_entry_abs_pct = 0.005; // 50 bps
    cfg.basis_direction_switch_exit_abs_pct = 0.001;
    QTrading::Intent::BasisArbitrageIntentBuilder builder(cfg);

    QTrading::Signal::SignalDecision signal{};
    signal.ts_ms = 1000;
    signal.strategy = "basis_arbitrage";
    signal.status = QTrading::Signal::SignalStatus::Active;
    signal.urgency = QTrading::Signal::SignalUrgency::Low;
    signal.confidence = 0.8;

    // Positive basis: keep receive_funding side (Long spot, Short perp).
    auto pos = builder.build(signal, MakeMarketWithBasis(1000, 101.0));
    ASSERT_EQ(pos.legs.size(), 2u);
    EXPECT_EQ(pos.legs[0].instrument, "BTCUSDT_SPOT");
    EXPECT_EQ(pos.legs[0].side, QTrading::Intent::TradeSide::Long);
    EXPECT_EQ(pos.legs[1].instrument, "BTCUSDT_PERP");
    EXPECT_EQ(pos.legs[1].side, QTrading::Intent::TradeSide::Short);

    // Negative basis would request spot-short/perp-long, which is intentionally
    // suppressed in the current executable basis stack.
    signal.ts_ms = 2000;
    auto neg = builder.build(signal, MakeMarketWithBasis(2000, 99.0));
    ASSERT_EQ(neg.legs.size(), 2u);
    EXPECT_EQ(neg.legs[0].side, QTrading::Intent::TradeSide::Long);
    EXPECT_EQ(neg.legs[1].side, QTrading::Intent::TradeSide::Short);
}
