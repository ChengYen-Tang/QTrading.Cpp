#pragma once

#include "IDataPreprocess.hpp"
#include <Dto/Market/Binance/Kline.hpp>
#include <Exchanges/IExchange.h>

using namespace QTrading::DataPreprocess;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Infra::Exchanges;

namespace QTrading::DataPreprocess::Simulator {

    /// @brief  Simulates Binance by forwarding raw KlineDto from an exchange.
    class Binance : public IDataPreprocess<std::shared_ptr<KlineDto>>
    {
    public:
        /// @brief  Constructor.
        /// @param exchange  Source exchange providing minute‐level KlineDto.
        Binance(shared_ptr<IExchange<std::shared_ptr<KlineDto>>> exchange);

    private:
        shared_ptr<IExchange<std::shared_ptr<KlineDto>>> exchange;  ///< Underlying exchange

    protected:
        /// @copydoc IDataPreprocess::run()
        void run() override;
    };
}
