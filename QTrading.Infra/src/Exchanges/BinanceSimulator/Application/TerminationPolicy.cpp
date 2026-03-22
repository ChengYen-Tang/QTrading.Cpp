#include "Exchanges/BinanceSimulator/Application/TerminationPolicy.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

bool TerminationPolicy::IsReplayExhausted(const State::StepKernelState& state) noexcept
{
    return state.next_ts_heap.empty();
}

void TerminationPolicy::CloseChannels(BinanceExchange& exchange, State::StepKernelState& state) noexcept
{
    if (state.channels_closed) {
        return;
    }
    exchange.close();
    state.channels_closed = true;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
