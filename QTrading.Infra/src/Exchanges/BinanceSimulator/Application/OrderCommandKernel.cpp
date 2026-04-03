#include "Exchanges/BinanceSimulator/Application/OrderCommandKernel.hpp"

#include <algorithm>
#include <utility>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Domain/AsyncOrderLatencyScheduler.hpp"
#include "Exchanges/BinanceSimulator/Domain/BinanceRejectSurface.hpp"
#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Application {

OrderCommandKernel::OrderCommandKernel(BinanceExchange& exchange) noexcept
    : exchange_(exchange)
{
}

bool OrderCommandKernel::PlaceSpotLimit(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::SpotLimit;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    request.symbol = symbol;
    request.quantity = quantity;
    request.price = price;
    request.side = side;
    request.reduce_only = reduce_only;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::PlaceSpotMarket(const std::string& symbol, double quantity,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::SpotMarket;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    request.symbol = symbol;
    request.quantity = quantity;
    request.price = 0.0;
    request.side = side;
    request.reduce_only = reduce_only;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::PlaceSpotMarketQuote(const std::string& symbol, double quote_order_qty,
    QTrading::Dto::Trading::OrderSide side, bool reduce_only, const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::SpotMarketQuote;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    request.symbol = symbol;
    request.quantity = 0.0;
    request.price = 0.0;
    request.quote_order_qty = quote_order_qty;
    request.side = side;
    request.reduce_only = reduce_only;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::PlacePerpLimit(const std::string& symbol, double quantity, double price,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only, const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::PerpLimit;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    request.symbol = symbol;
    request.quantity = quantity;
    request.price = price;
    request.side = side;
    request.position_side = position_side;
    request.reduce_only = reduce_only;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::PlacePerpMarket(const std::string& symbol, double quantity,
    QTrading::Dto::Trading::OrderSide side, QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only, const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::PerpMarket;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    request.symbol = symbol;
    request.quantity = quantity;
    request.price = 0.0;
    request.side = side;
    request.position_side = position_side;
    request.reduce_only = reduce_only;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::PlacePerpClosePosition(const std::string& symbol, QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side, double price,
    const std::string& client_order_id, Account::SelfTradePreventionMode stp_mode) const
{
    Contracts::OrderCommandRequest request{};
    request.kind = Contracts::OrderCommandKind::PerpClosePosition;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    request.symbol = symbol;
    request.quantity = 0.0;
    request.price = price;
    request.side = side;
    request.position_side = position_side;
    request.reduce_only = false;
    request.close_position = true;
    request.client_order_id = client_order_id;
    request.stp_mode = static_cast<int>(stp_mode);
    return submit_(request);
}

bool OrderCommandKernel::SetPositionMode(bool hedge_mode) const
{
    std::optional<Contracts::OrderRejectInfo> reject{};
    return Domain::OrderEntryService::SetPositionMode(
        *exchange_.runtime_state_,
        *exchange_.step_kernel_state_,
        hedge_mode,
        reject);
}

void OrderCommandKernel::CancelOpenOrders(
    QTrading::Dto::Trading::InstrumentType instrument_type, const std::string& symbol) const
{
    Domain::OrderEntryService::CancelOpenOrders(
        *exchange_.runtime_state_,
        *exchange_.step_kernel_state_,
        instrument_type,
        symbol);
}

void OrderCommandKernel::FlushDeferredForStep(uint64_t step_seq) const
{
    auto& runtime_state = *exchange_.runtime_state_;
    while (!runtime_state.deferred_order_commands.empty()) {
        const auto& deferred = runtime_state.deferred_order_commands.front();
        if (deferred.due_step > step_seq) {
            break;
        }

        std::optional<Contracts::OrderRejectInfo> reject{};
        const bool accepted = Domain::OrderEntryService::Execute(
            *exchange_.runtime_state_,
            exchange_.account_state(),
            *exchange_.step_kernel_state_,
            deferred.request,
            reject);

        auto ack = build_ack_base_(deferred.request, deferred.request_id, deferred.submitted_step, deferred.due_step);
        ack.status = accepted ? Contracts::AsyncOrderAck::Status::Accepted : Contracts::AsyncOrderAck::Status::Rejected;
        ack.resolved_step = step_seq;
        if (!accepted && reject.has_value()) {
            ack.reject_code = static_cast<int>(reject->code);
            ack.reject_message = reject->message != nullptr ? reject->message : "";
            const auto mapped = Domain::BinanceRejectSurface::MapToBinanceError(reject);
            ack.binance_error_code = mapped.first;
            ack.binance_error_message = mapped.second;
        }
        runtime_state.async_order_acks.emplace_back(std::move(ack));
        runtime_state.deferred_order_commands.pop_front();
    }
}

bool OrderCommandKernel::submit_(const Contracts::OrderCommandRequest& request) const
{
    auto& runtime_state = *exchange_.runtime_state_;
    const uint64_t submitted_step = exchange_.step_kernel_state_->step_seq;
    auto maybe_ticket = Domain::AsyncOrderLatencyScheduler::TrySchedule(
        runtime_state.order_latency_bars,
        submitted_step,
        runtime_state.next_async_order_request_id,
        [&](const Domain::AsyncOrderScheduleTicket& ticket) {
            auto pending = build_ack_base_(request, ticket.request_id, ticket.submitted_step, ticket.due_step);
            pending.status = Contracts::AsyncOrderAck::Status::Pending;
            runtime_state.async_order_acks.emplace_back(std::move(pending));
        },
        [&](const Domain::AsyncOrderScheduleTicket& ticket) {
            Contracts::DeferredOrderCommand deferred{};
            deferred.request_id = ticket.request_id;
            deferred.submitted_step = ticket.submitted_step;
            deferred.due_step = ticket.due_step;
            deferred.request = request;
            runtime_state.deferred_order_commands.emplace_back(std::move(deferred));
        });
    if (maybe_ticket.has_value()) {
        return true;
    }

    std::optional<Contracts::OrderRejectInfo> reject{};
    return Domain::OrderEntryService::Execute(
        *exchange_.runtime_state_,
        exchange_.account_state(),
        *exchange_.step_kernel_state_,
        request,
        reject);
}

Contracts::AsyncOrderAck OrderCommandKernel::build_ack_base_(
    const Contracts::OrderCommandRequest& request, uint64_t request_id, uint64_t submitted_step, uint64_t due_step)
{
    Contracts::AsyncOrderAck ack{};
    ack.request_id = request_id;
    ack.instrument_type = request.instrument_type;
    ack.symbol = request.symbol;
    ack.quantity = request.quantity;
    ack.price = request.price;
    ack.side = request.side;
    ack.position_side = request.position_side;
    ack.reduce_only = request.reduce_only;
    ack.submitted_step = submitted_step;
    ack.due_step = due_step;
    ack.client_order_id = request.client_order_id;
    ack.stp_mode = request.stp_mode;
    ack.close_position = request.close_position;
    return ack;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Application
