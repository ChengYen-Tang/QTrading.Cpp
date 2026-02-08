#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using QTrading::Dto::Trading::OrderSide;

namespace QTrading::Infra::Exchanges::BinanceSim {

bool BinanceExchange::SpotApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    if (owner_.order_latency_bars_ > 0) {
        const uint64_t due = owner_.processed_steps_ + static_cast<uint64_t>(owner_.order_latency_bars_);
        owner_.enqueue_deferred_order_locked_(due, [symbol, quantity, price, side, reduce_only](Account& acc) {
            (void)acc.spot.place_order(symbol, quantity, price, side, reduce_only);
        });
        return true;
    }
    return owner_.account_engine_->spot.place_order(symbol, quantity, price, side, reduce_only);
}

bool BinanceExchange::SpotApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    bool reduce_only)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    if (owner_.order_latency_bars_ > 0) {
        const uint64_t due = owner_.processed_steps_ + static_cast<uint64_t>(owner_.order_latency_bars_);
        owner_.enqueue_deferred_order_locked_(due, [symbol, quantity, side, reduce_only](Account& acc) {
            (void)acc.spot.place_order(symbol, quantity, side, reduce_only);
        });
        return true;
    }
    return owner_.account_engine_->spot.place_order(symbol, quantity, side, reduce_only);
}

void BinanceExchange::SpotApi::close_position(const std::string& symbol, double price)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->spot.close_position(symbol, price);
}

void BinanceExchange::SpotApi::cancel_open_orders(const std::string& symbol)
{
    std::lock_guard<std::mutex> lk(owner_.account_mtx_);
    owner_.account_engine_->spot.cancel_open_orders(symbol);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
