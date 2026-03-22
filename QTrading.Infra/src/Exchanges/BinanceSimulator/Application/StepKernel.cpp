#include "Exchanges/BinanceSimulator/Application/StepKernel.hpp"
#include "Exchanges/BinanceSimulator/Application/LegacyStepBackend.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {
namespace {

struct ModeSelection {
    BinanceExchange::CoreMode requested_mode{ BinanceExchange::CoreMode::LegacyOnly };
    bool force_legacy_only{ false };
};

ModeSelection resolve_mode_selection()
{
    return ModeSelection{};
}

bool compute_fallback_to_legacy(const ModeSelection& mode)
{
    return mode.force_legacy_only || mode.requested_mode != BinanceExchange::CoreMode::LegacyOnly;
}

BinanceExchange::SessionReplayCoexistenceDiagnostic build_coexistence_diag(
    const ModeSelection& mode,
    bool fallback_to_legacy)
{
    BinanceExchange::SessionReplayCoexistenceDiagnostic diag{};
    diag.requested_mode = mode.requested_mode;
    diag.effective_mode = BinanceExchange::CoreMode::LegacyOnly;
    diag.production_default_legacy_only = false;
    diag.force_legacy_only = mode.force_legacy_only;
    diag.shadow_compare_enabled = false;
    diag.v2_explicit_enabled = false;
    diag.compare_artifact_enabled = false;
    diag.fallback_to_legacy = fallback_to_legacy;
    diag.fail_close_protected = true;
    if (mode.force_legacy_only && mode.requested_mode != BinanceExchange::CoreMode::LegacyOnly) {
        diag.reason = "force legacy-only flag active during step; executing legacy path.";
    }
    else if (fallback_to_legacy) {
        diag.reason = "hard-prune legacy-only route active; non-legacy request executed as legacy.";
    }
    else {
        diag.reason = "session/replay step executed.";
    }
    return diag;
}

BinanceExchange::BinanceExchangeFacadeBridgeDiagnostic build_facade_bridge_diag(
    const ModeSelection& mode,
    const BinanceExchange::RunStepResult& result,
    uint64_t step_seq,
    uint64_t ts_exchange)
{
    BinanceExchange::BinanceExchangeFacadeBridgeDiagnostic diag{};
    diag.requested_mode = mode.requested_mode;
    diag.effective_mode = BinanceExchange::CoreMode::LegacyOnly;
    diag.progressed = result.progressed;
    diag.fallback_to_legacy = compute_fallback_to_legacy(mode);
    diag.used_v2_session_core = false;
    diag.preserve_capture_boundary = true;
    diag.preserve_log_batch_boundary = true;
    diag.preserve_timestamp_semantics = true;
    diag.step_seq = step_seq;
    diag.ts_exchange = ts_exchange;
    if (diag.fallback_to_legacy) {
        diag.reason = "Facade bridge routed to legacy-compatible session path.";
    }
    else {
        diag.reason = "Facade bridge preserved legacy contract boundaries.";
    }
    return diag;
}

} // namespace

StepKernel::StepKernel(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

bool StepKernel::run() const
{
    auto mode = resolve_mode_selection();
    mode.requested_mode = exchange_.core_mode_.load(std::memory_order_acquire);
    mode.force_legacy_only = exchange_.force_legacy_only_.load(std::memory_order_acquire);
    const LegacyStepBackend legacy_backend(exchange_);
    auto result = legacy_backend.run_legacy();
    result.fallback_to_legacy = compute_fallback_to_legacy(mode);
    const bool fallback_to_legacy = result.fallback_to_legacy;
    uint64_t step_seq = 0;
    uint64_t ts_exchange = 0;
    {
        std::lock_guard<std::mutex> lk(exchange_.state_mtx_);
        step_seq = exchange_.log_ctx_.step_seq;
        ts_exchange = exchange_.last_step_ts_;
    }

    exchange_.record_session_replay_coexistence_diagnostic_(
        build_coexistence_diag(mode, fallback_to_legacy));
    BinanceExchange::StepCompareDiagnostic compare_diag{};
    compare_diag.mode = mode.requested_mode;
    compare_diag.compared = false;
    compare_diag.matched = true;
    compare_diag.reason = "Step compare disabled under hard-prune legacy-only step core.";
    compare_diag.legacy = exchange_.build_step_compare_snapshot_(result);
    exchange_.record_compare_diagnostic_(std::move(compare_diag));
    exchange_.record_binance_exchange_facade_bridge_diagnostic_(
        build_facade_bridge_diag(mode, result, step_seq, ts_exchange));

    return result.progressed;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
