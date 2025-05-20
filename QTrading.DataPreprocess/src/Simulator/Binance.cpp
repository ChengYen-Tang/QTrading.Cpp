#include "Simulator/Binance.hpp"
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::DataPreprocess::Simulator;
using namespace QTrading::Utils::Queue;

/// @brief  Initialize simulator with given exchange source.
/// @param  exchange  Shared pointer to underlying exchange.
Binance::Binance(shared_ptr<IExchange<std::shared_ptr<KlineDto>>> exchange)
    : exchange(exchange)
{
    this->market_channel =
        shared_ptr<Channel<std::shared_ptr<KlineDto>>>(
            ChannelFactory::CreateBoundedChannel<std::shared_ptr<KlineDto>>(1));
}

/// @brief  Continuously read from exchange channel and forward to own channel.
void Binance::run()
{
    auto exchange_channel = exchange->get_market_channel();
    while (!stopFlag.load()) {
        if (!exchange_channel->IsClosed()) {
            std::optional<std::shared_ptr<KlineDto>> kline = exchange_channel->Receive();
            if (kline.has_value()) {
                market_channel->Send(kline.value());
            }
        }
    }
}
