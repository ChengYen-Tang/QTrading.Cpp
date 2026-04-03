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

/// Validates, books, and mutates order-entry state for normalized facade commands.
class OrderEntryService final {
public:
    /// Executes one normalized order command against runtime/account/step state.
    static bool Execute(
        State::BinanceExchangeRuntimeState& runtime_state,
        Account& account,
        State::StepKernelState& step_state,
        const Contracts::OrderCommandRequest& request,
        std::optional<Contracts::OrderRejectInfo>& reject);

    /// Toggles hedge-mode state when current open exposure allows it.
    static bool SetPositionMode(
        State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        bool hedge_mode,
        std::optional<Contracts::OrderRejectInfo>& reject);

    /// Recomputes spot/perp open-order margin reservations from the live order book.
    static void SyncOpenOrderMargins(
        State::BinanceExchangeRuntimeState& runtime_state,
        const State::StepKernelState& step_state);

    /// Cancels one open order by internal order id.
    static bool CancelOrderById(
        State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        int order_id);

    /// Cancels all open orders for one symbol/instrument lane.
    static void CancelOpenOrders(
        State::BinanceExchangeRuntimeState& runtime_state,
        State::StepKernelState& step_state,
        QTrading::Dto::Trading::InstrumentType instrument_type,
        const std::string& symbol);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
