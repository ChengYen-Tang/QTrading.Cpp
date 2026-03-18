#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class LegacyLogRowKind : uint8_t {
    Snapshot = 0,
    Event = 1,
};

struct LegacyLogPayloadField {
    std::string key;
    std::string value;
};

struct LegacyLogCompareRow {
    uint64_t arrival_index{ 0 };
    uint64_t batch_boundary{ 0 };
    int32_t module_id{ -1 };
    std::string module_name;
    uint64_t ts_exchange{ 0 };
    uint64_t ts_local{ 0 };
    uint64_t run_id{ 0 };
    uint64_t step_seq{ 0 };
    uint64_t event_seq{ 0 };
    std::string symbol;
    LegacyLogRowKind row_kind{ LegacyLogRowKind::Event };
    std::vector<LegacyLogPayloadField> payload_fields;
};

enum class LegacyLogRowOrderingRule : uint8_t {
    Arrival = 0,
    Business = 1,
};

struct LegacyLogRowCompareRules {
    LegacyLogRowOrderingRule ordering{ LegacyLogRowOrderingRule::Arrival };
    bool strict_row_kind{ true };
    bool strict_payload_fields{ true };
    bool strict_step_seq{ true };
    bool strict_event_seq{ true };
    bool strict_ts_exchange{ true };
    bool strict_ts_local{ true };
    bool strict_module_ordering{ true };
    bool strict_batch_boundary{ true };
    std::vector<int32_t> module_scope_ids{};
};

struct LegacyLogRowCompareReport {
    bool matched{ true };
    uint64_t legacy_row_count{ 0 };
    uint64_t candidate_row_count{ 0 };
    uint64_t compared_row_count{ 0 };
    std::optional<uint64_t> first_divergent_row{ std::nullopt };
    std::optional<ReplayMismatch> first_mismatch{ std::nullopt };
    std::vector<ReplayMismatch> mismatches;
};

class LegacyLogRowComparer final {
public:
    LegacyLogRowCompareReport Compare(
        const std::vector<LegacyLogCompareRow>& legacy_rows,
        const std::vector<LegacyLogCompareRow>& candidate_rows,
        const LegacyLogRowCompareRules& rules = {}) const;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
