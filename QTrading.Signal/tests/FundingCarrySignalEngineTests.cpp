#include "Signal/FundingCarrySignalEngine.hpp"

#include <optional>
#include <gtest/gtest.h>

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
    dto->klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
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
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    return dto;
}

} // namespace

TEST(FundingCarrySignalEngineTests, ActiveWhenBothSymbolsPresent)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(MakeMarket(1000, true, true));
    EXPECT_EQ(signal.strategy, "funding_carry");
    EXPECT_EQ(signal.symbol, "BTCUSDT_PERP");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}

TEST(FundingCarrySignalEngineTests, InactiveWhenMissingSymbol)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(MakeMarket(1000, true, false));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, InactiveWhenFundingBelowThreshold)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.0001, 1.0, 1.0, 0 });
    auto signal = engine.on_market(MakeMarket(1000, true, true, 100.02));
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Inactive);
}

TEST(FundingCarrySignalEngineTests, ExitAndCooldown)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0002, 0.00005, 0.01, 0.02, 1000, 0 });
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
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_CASH", "BTCUSDT_SWAP", 0.0, 0.0, 1.0, 1.0, 0 });
    auto signal = engine.on_market(
        MakeMarketCustomSymbols(1000, "BTCUSDT_CASH", "BTCUSDT_SWAP", true, true, 100.0, 100.5));
    EXPECT_EQ(signal.strategy, "funding_carry");
    EXPECT_EQ(signal.symbol, "BTCUSDT_SWAP");
    EXPECT_EQ(signal.status, QTrading::Signal::SignalStatus::Active);
}
