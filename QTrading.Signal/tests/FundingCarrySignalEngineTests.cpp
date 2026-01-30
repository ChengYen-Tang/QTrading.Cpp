#include "Signal/FundingCarrySignalEngine.hpp"

#include <gtest/gtest.h>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(
    unsigned long long ts,
    bool include_spot,
    bool include_perp)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto spot(ts, 0, 0, 0, 100.0, 0, ts, 0, 0, 0, 0);
    QTrading::Dto::Market::Binance::KlineDto perp(ts, 0, 0, 0, 100.5, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back("BTCUSDT_SPOT");
    symbols->push_back("BTCUSDT_PERP");
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    return dto;
}

} // namespace

TEST(FundingCarrySignalEngineTests, ActiveWhenBothSymbolsPresent)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    auto signal = engine.on_market(MakeMarket(1000, true, true));
    EXPECT_EQ(signal.strategy, "funding_carry");
    EXPECT_EQ(signal.symbol, "BTCUSDT_PERP");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, InactiveWhenMissingSymbol)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP" });
    auto signal = engine.on_market(MakeMarket(1000, true, false));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}