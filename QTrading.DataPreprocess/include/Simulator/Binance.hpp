#pragma once

#include "IDataPreprocess.hpp"
#include <Dto/Market/Binance/Kline.hpp>
#include <Exanges/IExchange.h>

using namespace QTrading::DataPreprocess;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Infra::Exanges;

namespace QTrading::DataPreprocess::Simulator {
	class Binance : public IDataPreprocess<std::shared_ptr<KlineDto>>
	{
	public:
		Binance(shared_ptr<IExchange<std::shared_ptr<KlineDto>>> exchange);
	private:
		shared_ptr<IExchange<std::shared_ptr<KlineDto>>> exchange;
	protected:
		void run() override;
	};
}
