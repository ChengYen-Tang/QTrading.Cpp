#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/Observability/Compare/CompareNamespaceAlias.hpp"

namespace {

TEST(ObservabilityCompareAliasTests, CompareNamespaceAliasMapsToDiagnosticsCompare)
{
    const auto quick =
        QTrading::Infra::Exchanges::BinanceSim::Observability::Compare::ReplayCompareCiGate::
        BuildCompareQuickPlan();
    const auto full =
        QTrading::Infra::Exchanges::BinanceSim::Observability::Compare::ReplayCompareCiGate::
        BuildComparePackFullPlan();

    EXPECT_EQ(quick.gate_name, "compare-quick");
    EXPECT_EQ(full.gate_name, "compare-pack-full");
}

} // namespace
