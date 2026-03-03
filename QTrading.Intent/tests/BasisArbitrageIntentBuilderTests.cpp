#include "Intent/BasisArbitrageIntentBuilder.hpp"

#include <gtest/gtest.h>
#include <memory>

namespace {

std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto> MakeMarket(unsigned long long ts)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
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
    EXPECT_EQ(intent.reason, "basis_arbitrage");
    EXPECT_EQ(intent.legs.size(), 2u);
    EXPECT_EQ(intent.intent_id.rfind("basis_arbitrage:", 0), 0u);
}
