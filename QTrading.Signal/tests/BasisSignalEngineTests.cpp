#include "Signal/BasisSignalEngine.hpp"

#include <gtest/gtest.h>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(
    unsigned long long ts,
    double spot_close,
    double perp_close)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto spot(ts, 0, 0, 0, spot_close, 0, ts, 0, 0, 0, 0);
    QTrading::Dto::Market::Binance::KlineDto perp(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
    dto->klines["BTCUSDT_SPOT"] = spot;
    dto->klines["BTCUSDT_PERP"] = perp;
    return dto;
}

} // namespace

TEST(BasisSignalEngineTests, ActivatesWhenZScoreAboveThreshold)
{
    QTrading::Signal::BasisSignalEngine engine({
        "BTCUSDT_SPOT",
        "BTCUSDT_PERP",
        3,
        0.5,
        0.2
    });

    auto s1 = engine.on_market(MakeMarket(1, 100.0, 100.0));
    auto s2 = engine.on_market(MakeMarket(2, 100.0, 100.0));
    auto s3 = engine.on_market(MakeMarket(3, 100.0, 110.0));

    EXPECT_EQ(s3.symbol, "BTCUSDT_PERP");
    EXPECT_EQ(s3.strategy, "basis_zscore");
    EXPECT_EQ(s3.status, QTrading::Signal::SignalStatus::Active);
}
