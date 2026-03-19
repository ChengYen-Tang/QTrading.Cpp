#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/LegacyLogRowCompare.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::LegacyLogCompareRow MakeRow(
    uint64_t arrival_index,
    int32_t module_id,
    std::string module_name,
    uint64_t ts_exchange,
    uint64_t ts_local,
    uint64_t step_seq,
    uint64_t event_seq,
    ReplayCompare::LegacyLogRowKind row_kind = ReplayCompare::LegacyLogRowKind::Event,
    uint64_t batch_boundary = 0)
{
    ReplayCompare::LegacyLogCompareRow row{};
    row.arrival_index = arrival_index;
    row.batch_boundary = batch_boundary;
    row.module_id = module_id;
    row.module_name = std::move(module_name);
    row.ts_exchange = ts_exchange;
    row.ts_local = ts_local;
    row.run_id = 1;
    row.step_seq = step_seq;
    row.event_seq = event_seq;
    row.symbol = "BTCUSDT";
    row.row_kind = row_kind;
    row.payload_fields = {
        {"k", "v"},
        {"qty", "1.0"}
    };
    return row;
}

std::optional<ReplayCompare::ReplayMismatch> FindMismatch(
    const ReplayCompare::LegacyLogRowCompareReport& report,
    const std::string& field)
{
    for (const auto& mismatch : report.mismatches) {
        if (mismatch.field == field) {
            return mismatch;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(LegacyLogRowCompareTests, IdenticalRowsPassAndNoDivergence)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    const std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 11, "OrderEvent", 1000, 1002, 1, 2),
    };
    const auto candidate = legacy;

    const auto report = comparer.Compare(legacy, candidate);
    EXPECT_TRUE(report.matched);
    EXPECT_FALSE(report.first_divergent_row.has_value());
    EXPECT_FALSE(report.first_mismatch.has_value());
    EXPECT_EQ(report.compared_row_count, 2u);
}

TEST(LegacyLogRowCompareTests, DetectsRowCountMismatchAndPresenceAbsenceSemantics)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    const std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 11, "OrderEvent", 1000, 1002, 1, 2),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
    };

    const auto report = comparer.Compare(legacy, candidate);
    EXPECT_FALSE(report.matched);
    EXPECT_EQ(report.legacy_row_count, 2u);
    EXPECT_EQ(report.candidate_row_count, 1u);
    EXPECT_TRUE(FindMismatch(report, "row.count").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.presence").has_value());
}

TEST(LegacyLogRowCompareTests, DetectsOrderingMismatchInArrivalMode)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 11, "OrderEvent", 1000, 1002, 1, 2),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 11, "OrderEvent", 1000, 1002, 1, 2),
        MakeRow(1, 10, "AccountEvent", 1000, 1001, 1, 1),
    };

    ReplayCompare::LegacyLogRowCompareRules rules{};
    rules.ordering = ReplayCompare::LegacyLogRowOrderingRule::Arrival;
    const auto report = comparer.Compare(legacy, candidate, rules);
    EXPECT_FALSE(report.matched);
    EXPECT_TRUE(FindMismatch(report, "row.module_id").has_value());
}

TEST(LegacyLogRowCompareTests, BusinessOrderingRuleStabilizesArrivalDifferences)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(1, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(0, 11, "OrderEvent", 1000, 1002, 1, 2),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 11, "OrderEvent", 1000, 1002, 1, 2),
    };

    ReplayCompare::LegacyLogRowCompareRules rules{};
    rules.ordering = ReplayCompare::LegacyLogRowOrderingRule::Business;
    const auto report = comparer.Compare(legacy, candidate, rules);
    EXPECT_TRUE(report.matched);
}

TEST(LegacyLogRowCompareTests, DetectsModuleMismatchByIdAndName)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 99, "UnexpectedModule", 1000, 1001, 1, 1),
    };

    const auto report = comparer.Compare(legacy, candidate);
    EXPECT_FALSE(report.matched);
    EXPECT_TRUE(FindMismatch(report, "row.module_id").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.module_name").has_value());
}

TEST(LegacyLogRowCompareTests, DetectsTimestampSemanticsMismatch)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 10, "AccountEvent", 2000, 3000, 1, 1),
    };

    const auto report = comparer.Compare(legacy, candidate);
    EXPECT_FALSE(report.matched);
    EXPECT_TRUE(FindMismatch(report, "row.ts_exchange").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.ts_local").has_value());
}

TEST(LegacyLogRowCompareTests, DetectsPayloadKeyValueMismatch)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    auto legacy_row = MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1);
    auto candidate_row = legacy_row;
    candidate_row.payload_fields = {
        {"k", "different"},
        {"new_key", "1"}
    };

    const auto report = comparer.Compare({legacy_row}, {candidate_row});
    EXPECT_FALSE(report.matched);
    EXPECT_TRUE(FindMismatch(report, "payload.k").has_value());
    EXPECT_TRUE(FindMismatch(report, "payload.qty").has_value());
    EXPECT_TRUE(FindMismatch(report, "payload.new_key").has_value());
}

TEST(LegacyLogRowCompareTests, PayloadNormalizationTrimsWhitespaceAndUsesNumericTolerance)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    auto legacy_row = MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1);
    legacy_row.payload_fields = {
        {"qty", "1.0000000000"},
        {"price", "100.0"},
    };

    auto candidate_row = legacy_row;
    candidate_row.payload_fields = {
        {"qty", " 1.0000000005 "},
        {"price", "100.0000000004"},
    };

    const auto normalized_report = comparer.Compare({legacy_row}, {candidate_row});
    EXPECT_TRUE(normalized_report.matched);

    ReplayCompare::LegacyLogRowCompareRules strict_rules{};
    strict_rules.payload_numeric_abs_tolerance = 1e-12;
    const auto strict_report = comparer.Compare({legacy_row}, {candidate_row}, strict_rules);
    EXPECT_FALSE(strict_report.matched);
    EXPECT_TRUE(FindMismatch(strict_report, "payload.qty").has_value());
}

TEST(LegacyLogRowCompareTests, ProtectsStepEventSeqModuleOrderingAndBatchBoundarySemantics)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    auto legacy_row = MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1, ReplayCompare::LegacyLogRowKind::Snapshot, 7);
    auto candidate_row = legacy_row;
    candidate_row.step_seq = 9;
    candidate_row.event_seq = 5;
    candidate_row.batch_boundary = 99;
    candidate_row.row_kind = ReplayCompare::LegacyLogRowKind::Event;

    const auto report = comparer.Compare({legacy_row}, {candidate_row});
    EXPECT_FALSE(report.matched);
    EXPECT_TRUE(FindMismatch(report, "row.step_seq").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.event_seq").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.batch_boundary").has_value());
    EXPECT_TRUE(FindMismatch(report, "row.kind").has_value());
}

TEST(LegacyLogRowCompareTests, ModuleScopeLimitsComparedRows)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 99, "Other", 1000, 1002, 1, 2),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 99, "Other", 9000, 9001, 1, 2),
    };

    ReplayCompare::LegacyLogRowCompareRules rules{};
    rules.module_scope_ids = {10};
    const auto report = comparer.Compare(legacy, candidate, rules);
    EXPECT_TRUE(report.matched);
    EXPECT_EQ(report.compared_row_count, 1u);
}

TEST(LegacyLogRowCompareTests, FirstDivergentRowLocalizationIncludesRowAndSequence)
{
    ReplayCompare::LegacyLogRowComparer comparer;
    std::vector<ReplayCompare::LegacyLogCompareRow> legacy{
        MakeRow(0, 10, "AccountEvent", 1000, 1001, 1, 1),
        MakeRow(1, 11, "OrderEvent", 1000, 1002, 1, 2),
        MakeRow(2, 12, "PositionEvent", 1000, 1003, 1, 3),
    };
    std::vector<ReplayCompare::LegacyLogCompareRow> candidate = legacy;
    candidate[1].payload_fields = {{"k", "bad"}};

    const auto report = comparer.Compare(legacy, candidate);
    EXPECT_FALSE(report.matched);
    ASSERT_TRUE(report.first_divergent_row.has_value());
    EXPECT_EQ(*report.first_divergent_row, 1u);
    ASSERT_TRUE(report.first_mismatch.has_value());
    EXPECT_EQ(report.first_mismatch->row_index, 1u);
    EXPECT_EQ(report.first_mismatch->step_index, 1u);
    EXPECT_EQ(report.first_mismatch->event_seq, 2u);
}
