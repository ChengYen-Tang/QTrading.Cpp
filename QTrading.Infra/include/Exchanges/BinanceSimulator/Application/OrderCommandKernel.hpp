#pragma once

#include <optional>
#include <string>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderCommandRequest.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderRejectInfo.hpp"
#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

/// Minimal order command execution/scheduling kernel used by Spot/Perp facades.
/// It supports sync reject and async pending/resolved ack without matching/funding logic.
class OrderCommandKernel final {
public:
    explicit OrderCommandKernel(BinanceExchange& exchange) noexcept;

    bool PlaceSpotLimit(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode) const;
    bool PlaceSpotMarket(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode) const;
    bool PlaceSpotMarketQuote(const std::string& symbol, double quote_order_qty,
        QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode) const;

    bool PlacePerpLimit(const std::string& symbol, double quantity, double price,
        QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only, const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode) const;
    bool PlacePerpMarket(const std::string& symbol, double quantity,
        QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only, const std::string& client_order_id,
        Account::SelfTradePreventionMode stp_mode) const;
    bool PlacePerpClosePosition(const std::string& symbol, QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side, double price,
        const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode) const;

    bool SetPositionMode(bool hedge_mode) const;

    void CancelOpenOrders(QTrading::Dto::Trading::InstrumentType instrument_type, const std::string& symbol) const;

    /// Called by StepKernel once per successful step to resolve due async requests.
    void FlushDeferredForStep(uint64_t step_seq) const;

private:
    bool submit_(const Contracts::OrderCommandRequest& request) const;
    static Contracts::AsyncOrderAck build_ack_base_(
        const Contracts::OrderCommandRequest& request, uint64_t request_id,
        uint64_t submitted_step, uint64_t due_step);

    BinanceExchange& exchange_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
