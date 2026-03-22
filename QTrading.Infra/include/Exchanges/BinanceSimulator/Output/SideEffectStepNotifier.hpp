#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class SideEffectStepNotifier final {
public:
    static Contracts::SideEffectStepSnapshot Initialize(
        const QTrading::Infra::Logging::StepLogContext& step_log_ctx,
        uint64_t state_version);

    static void DispatchExternalHook(
        BinanceExchange& owner,
        const Contracts::SideEffectStepSnapshot& snapshot);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
