#include "Exchanges/BinanceSimulator/Output/ChannelPublisher.hpp"

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/StepObservableContext.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

void ChannelPublisher::PublishStep(BinanceExchange& exchange, const StepObservableContext& context) noexcept
{
    // Fast guard path: skip publishes for terminal/empty contexts.
    if (context.replay_exhausted || !context.market_payload) {
        return;
    }

    if (auto market_channel = exchange.get_market_channel(); market_channel) {
        market_channel->Send(context.market_payload);
    }
}

void ChannelPublisher::ClosePublicChannels(BinanceExchange& exchange, const StepObservableContext& context) noexcept
{
    // Contract guard: only terminal contexts are allowed to close channels.
    if (!context.replay_exhausted) {
        return;
    }
    exchange.close();
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
