#include "Signal/BasisArbitrageSignalEngine.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <vector>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(
    unsigned long long ts,
    bool include_spot,
    bool include_perp,
    double perp_close = 100.5)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto spot(ts, 0, 0, 0, 100.0, 0, ts, 0, 0, 0, 0);
    QTrading::Dto::Market::Binance::KlineDto perp(ts, 0, 0, 0, perp_close, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back("BTCUSDT_SPOT");
    symbols->push_back("BTCUSDT_PERP");
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] =
        include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] =
        include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
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
