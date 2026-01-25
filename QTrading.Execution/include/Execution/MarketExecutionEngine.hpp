#pragma once

#include <memory>
#include <unordered_map>
#include "IExecutionEngine.hpp"
#include "Exchanges/IExchange.h"
#include "Dto/Market/Binance/MultiKline.hpp"

namespace QTrading::Execution {

/// @brief Execution engine that converts target notionals into market orders.
class MarketExecutionEngine final : public IExecutionEngine<
    std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>> {
public:
    struct Config {
        double min_notional = 5.0;
    };

    MarketExecutionEngine(
        std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
            std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
        Config cfg);

    std::vector<ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) override;

private:
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange_;
    Config cfg_;
    std::unordered_map<std::string, std::size_t> symbol_to_id_;
    bool has_symbol_index_{ false };
};

} // namespace QTrading::Execution
