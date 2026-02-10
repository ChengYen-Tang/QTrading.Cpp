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
    dto->klines_by_id.resize(symbols->size());
    dto->funding_by_id.resize(symbols->size());
    dto->klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
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
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = include_spot ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(spot) : std::nullopt;
    dto->klines_by_id[1] = include_perp ? std::optional<QTrading::Dto::Market::Binance::KlineDto>(perp) : std::nullopt;
    return dto;
}

} // namespace

TEST(FundingCarrySignalEngineTests, ActiveWhenBothSymbolsPresent)
{
    QTrading::Signal::FundingCarrySignalEngine engine({ "BTCUSDT_SPOT", "BTCUSDT_PERP", 0.0, 0.0, -1.0, 1.0, 1.0, 0 });
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
