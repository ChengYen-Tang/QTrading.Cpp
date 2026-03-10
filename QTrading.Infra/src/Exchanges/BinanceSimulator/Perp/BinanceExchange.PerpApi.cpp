#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace QTrading::Infra::Exchanges::BinanceSim {

bool BinanceExchange::PerpApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    if (owner_.order_latency_bars_ > 0) {
        const uint64_t req_id = owner_.next_async_order_request_id_++;
        const uint64_t submitted_step = owner_.processed_steps_;
        const uint64_t due = owner_.processed_steps_ + static_cast<uint64_t>(owner_.order_latency_bars_);
        owner_.push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
            req_id,
            BinanceExchange::AsyncOrderAck::Status::Pending,
            QTrading::Dto::Trading::InstrumentType::Perp,
            symbol,
            quantity,
            price,
            side,
            position_side,
            reduce_only,
            submitted_step,
            due,
            0,
            Account::OrderRejectInfo::Code::None,
            {},
            client_order_id,
            stp_mode
        });
        owner_.enqueue_deferred_order_locked_(due, [owner_ptr = &owner_, req_id, submitted_step, due, symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode](Account& acc, uint64_t step_seq) {
            if (owner_ptr->perp_opening_blocked_by_basis_stress_account_locked_(acc, symbol, side, position_side, reduce_only)) {
                owner_ptr->basis_stress_blocked_orders_.fetch_add(1, std::memory_order_relaxed);
                owner_ptr->push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
                    req_id,
                    BinanceExchange::AsyncOrderAck::Status::Rejected,
                    QTrading::Dto::Trading::InstrumentType::Perp,
                    symbol,
                    quantity,
                    price,
                    side,
                    position_side,
                    reduce_only,
                    submitted_step,
                    due,
                    step_seq,
                    Account::OrderRejectInfo::Code::None,
                    "Blocked by mark-index basis stress risk guard.",
                    client_order_id,
                    stp_mode,
                    -2010,
                    "NEW_ORDER_REJECTED"
                });
                return;
            }
            const bool ok = acc.perp.place_order(symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
            auto rej = acc.consume_last_order_reject_info();
            auto binance_reject = BinanceExchange::map_binance_reject_(rej);
            owner_ptr->push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
                req_id,
                ok ? BinanceExchange::AsyncOrderAck::Status::Accepted : BinanceExchange::AsyncOrderAck::Status::Rejected,
                QTrading::Dto::Trading::InstrumentType::Perp,
                symbol,
                quantity,
                price,
                side,
                position_side,
                reduce_only,
                submitted_step,
                due,
                step_seq,
                rej ? rej->code : Account::OrderRejectInfo::Code::None,
                rej ? rej->message : std::string{},
                client_order_id,
                stp_mode,
                binance_reject.first,
                std::move(binance_reject.second)
            });
        });
        return true;
    }
    Account& acc = *owner_.account_engine_;
    if (owner_.perp_opening_blocked_by_basis_stress_account_locked_(acc, symbol, side, position_side, reduce_only)) {
        owner_.basis_stress_blocked_orders_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return acc.perp.place_order(symbol, quantity, price, side, position_side, reduce_only, client_order_id, stp_mode);
}

bool BinanceExchange::PerpApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    Account::SelfTradePreventionMode stp_mode)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    if (owner_.order_latency_bars_ > 0) {
        const uint64_t req_id = owner_.next_async_order_request_id_++;
        const uint64_t submitted_step = owner_.processed_steps_;
        const uint64_t due = owner_.processed_steps_ + static_cast<uint64_t>(owner_.order_latency_bars_);
        owner_.push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
            req_id,
            BinanceExchange::AsyncOrderAck::Status::Pending,
            QTrading::Dto::Trading::InstrumentType::Perp,
            symbol,
            quantity,
            0.0,
            side,
            position_side,
            reduce_only,
            submitted_step,
            due,
            0,
            Account::OrderRejectInfo::Code::None,
            {},
            client_order_id,
            stp_mode
        });
        owner_.enqueue_deferred_order_locked_(due, [owner_ptr = &owner_, req_id, submitted_step, due, symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode](Account& acc, uint64_t step_seq) {
            if (owner_ptr->perp_opening_blocked_by_basis_stress_account_locked_(acc, symbol, side, position_side, reduce_only)) {
                owner_ptr->basis_stress_blocked_orders_.fetch_add(1, std::memory_order_relaxed);
                owner_ptr->push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
                    req_id,
                    BinanceExchange::AsyncOrderAck::Status::Rejected,
                    QTrading::Dto::Trading::InstrumentType::Perp,
                    symbol,
                    quantity,
                    0.0,
                    side,
                    position_side,
                    reduce_only,
                    submitted_step,
                    due,
                    step_seq,
                    Account::OrderRejectInfo::Code::None,
                    "Blocked by mark-index basis stress risk guard.",
                    client_order_id,
                    stp_mode,
                    -2010,
                    "NEW_ORDER_REJECTED"
                });
                return;
            }
            const bool ok = acc.perp.place_order(symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
            auto rej = acc.consume_last_order_reject_info();
            auto binance_reject = BinanceExchange::map_binance_reject_(rej);
            owner_ptr->push_async_order_ack_locked_(BinanceExchange::AsyncOrderAck{
                req_id,
                ok ? BinanceExchange::AsyncOrderAck::Status::Accepted : BinanceExchange::AsyncOrderAck::Status::Rejected,
                QTrading::Dto::Trading::InstrumentType::Perp,
                symbol,
                quantity,
                0.0,
                side,
                position_side,
                reduce_only,
                submitted_step,
                due,
                step_seq,
                rej ? rej->code : Account::OrderRejectInfo::Code::None,
                rej ? rej->message : std::string{},
                client_order_id,
                stp_mode,
                binance_reject.first,
                std::move(binance_reject.second)
            });
        });
        return true;
    }
    Account& acc = *owner_.account_engine_;
    if (owner_.perp_opening_blocked_by_basis_stress_account_locked_(acc, symbol, side, position_side, reduce_only)) {
        owner_.basis_stress_blocked_orders_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return acc.perp.place_order(symbol, quantity, side, position_side, reduce_only, client_order_id, stp_mode);
}

void BinanceExchange::PerpApi::close_position(const std::string& symbol, double price)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.close_position(symbol, price);
}

void BinanceExchange::PerpApi::close_position(const std::string& symbol,
    PositionSide position_side,
    double price)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.close_position(symbol, position_side, price);
}

void BinanceExchange::PerpApi::cancel_open_orders(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.cancel_open_orders(symbol);
}

void BinanceExchange::PerpApi::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->perp.set_symbol_leverage(symbol, new_leverage);
}

double BinanceExchange::PerpApi::get_symbol_leverage(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    return owner_.account_engine_->perp.get_symbol_leverage(symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
