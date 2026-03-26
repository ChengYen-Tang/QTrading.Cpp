#pragma once

#include <optional>
#include <string>

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderCommandRequest.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderRejectInfo.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
struct StepKernelState;
}
namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

class OrderEntryService final {
public:
    static bool Execute(
        State::BinanceExchangeRuntimeState& runtime_state,
        const Account& account,
        State::StepKernelState& step_state,
        const Contracts::OrderCommandRequest& request,
        std::optional<Contracts::OrderRejectInfo>& reject);

    static void CancelOpenOrders(
        State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        QTrading::Dto::Trading::InstrumentType instrument_type,
        const std::string& symbol);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
