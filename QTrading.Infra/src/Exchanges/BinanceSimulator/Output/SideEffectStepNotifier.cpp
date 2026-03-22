#include "Exchanges/BinanceSimulator/Output/SideEffectStepNotifier.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

Contracts::SideEffectStepSnapshot SideEffectStepNotifier::Initialize(
    const QTrading::Infra::Logging::StepLogContext& step_log_ctx,
    uint64_t state_version)
{
    Contracts::SideEffectStepSnapshot snapshot{};
    snapshot.run_id = step_log_ctx.run_id;
    snapshot.step_seq = step_log_ctx.step_seq;
    snapshot.ts_exchange = step_log_ctx.ts_exchange;
    snapshot.state_version = state_version;
    return snapshot;
}

void SideEffectStepNotifier::DispatchExternalHook(
    BinanceExchange& owner,
    const Contracts::SideEffectStepSnapshot& snapshot)
{
    if (owner.external_side_effect_hook_) {
        owner.external_side_effect_hook_(snapshot);
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
