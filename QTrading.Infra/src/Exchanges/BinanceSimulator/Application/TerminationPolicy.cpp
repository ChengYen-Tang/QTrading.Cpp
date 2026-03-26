#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/ChannelPublisher.hpp"
#include "Exchanges/BinanceSimulator/Output/StepObservableContext.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

bool TerminationPolicy::IsReplayExhausted(const State::StepKernelState& state) noexcept
{
    if (!state.next_ts_heap.empty()) {
        return false;
    }
    for (const auto has_next_funding : state.has_next_funding_ts) {
        if (has_next_funding != 0) {
            return false;
        }
    }
    return true;
}

void TerminationPolicy::CloseChannels(BinanceExchange& exchange, State::StepKernelState& state) noexcept
{
    // Idempotent close is required because step() can be called repeatedly
    // after termination in consumer loops.
    if (state.channels_closed) {
        return;
    }
    Output::StepObservableContext observable_ctx{};
    observable_ctx.step_seq = state.step_seq;
    observable_ctx.replay_exhausted = true;
    Output::ChannelPublisher::ClosePublicChannels(exchange, observable_ctx);
    state.channels_closed = true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
