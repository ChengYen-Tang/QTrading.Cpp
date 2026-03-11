#include "Exchanges/BinanceSimulator/Account/Account.hpp"

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

Account::SpotApi::SpotApi(Account& owner)
    : owner_(&owner)
{
}

QTrading::Dto::Account::BalanceSnapshot Account::SpotApi::get_balance() const
{
    return owner_->get_spot_balance();
}

double Account::SpotApi::get_cash_balance() const
{
    return owner_->get_spot_cash_balance();
}

bool Account::SpotApi::place_order(const std::string& symbol,
    double quantity,
    double price,
    OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    if (owner_->is_strict_symbol_registration_mode() && !owner_->has_explicit_instrument_symbol_(symbol)) {
        owner_->clear_last_order_reject_info_();
        return owner_->reject_order_(OrderRejectInfo::Code::UnknownSymbol, "Unknown symbol in strict symbol-registration mode");
    }
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    return owner_->place_order(symbol, quantity, price, side, PositionSide::Both, reduce_only, client_order_id, stp_mode);
}

bool Account::SpotApi::place_order(const std::string& symbol,
    double quantity,
    OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    if (owner_->is_strict_symbol_registration_mode() && !owner_->has_explicit_instrument_symbol_(symbol)) {
        owner_->clear_last_order_reject_info_();
        return owner_->reject_order_(OrderRejectInfo::Code::UnknownSymbol, "Unknown symbol in strict symbol-registration mode");
    }
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    return owner_->place_order(symbol, quantity, side, PositionSide::Both, reduce_only, client_order_id, stp_mode);
}

bool Account::SpotApi::place_market_order_quote(const std::string& symbol,
    double quote_order_qty,
    OrderSide side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    if (owner_->is_strict_symbol_registration_mode() && !owner_->has_explicit_instrument_symbol_(symbol)) {
        owner_->clear_last_order_reject_info_();
        return owner_->reject_order_(OrderRejectInfo::Code::UnknownSymbol, "Unknown symbol in strict symbol-registration mode");
    }
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    owner_->clear_last_order_reject_info_();

    if (!(quote_order_qty > 0.0)) {
        return owner_->reject_order_(OrderRejectInfo::Code::InvalidQuantity, "Invalid quote_order_qty <= 0");
    }

    const double trade_ref = owner_->get_last_trade_price_(symbol);
    if (!(trade_ref > 0.0)) {
        return owner_->reject_order_(OrderRejectInfo::Code::NotionalNoReferencePrice, "quoteOrderQty requires trade reference price");
    }

    double base_qty = 0.0;
    if (side == OrderSide::Buy) {
        const double denom = trade_ref * (1.0 + std::max(0.0, owner_->market_slippage_buffer_));
        if (!(denom > 0.0)) {
            return owner_->reject_order_(OrderRejectInfo::Code::NotionalNoReferencePrice, "quoteOrderQty conversion failed: invalid reference denominator");
        }
        base_qty = quote_order_qty / denom;
    }
    else {
        base_qty = quote_order_qty / trade_ref;
    }
    if (!(base_qty > 0.0)) {
        return owner_->reject_order_(OrderRejectInfo::Code::InvalidQuantity, "quoteOrderQty conversion produced non-positive base quantity");
    }

    const int expected_new_order_id = owner_->next_order_id_;

    const bool ok = owner_->place_order(symbol, base_qty, side, PositionSide::Both, reduce_only, client_order_id, stp_mode);
    if (!ok) {
        return false;
    }

    auto it = owner_->open_order_index_by_id_.find(expected_new_order_id);
    if (it == owner_->open_order_index_by_id_.end()) {
        return true;
    }
    const size_t idx = it->second;
    if (idx >= owner_->open_orders_.size()) {
        return true;
    }

    auto& ord = owner_->open_orders_[idx];
    if (ord.symbol == symbol && ord.instrument_type == InstrumentType::Spot) {
        ord.quote_order_qty = quote_order_qty;
    }

    return true;
}

void Account::SpotApi::close_position(const std::string& symbol, double price)
{
    owner_->set_instrument_type(symbol, InstrumentType::Spot);
    owner_->close_position(symbol, PositionSide::Both, price);
}

void Account::SpotApi::cancel_open_orders(const std::string& symbol)
{
    if (owner_->open_orders_.empty()) {
        return;
    }

    const bool changed = owner_->cancel_spot_open_orders_(symbol);
    if (!changed) {
        return;
    }

    owner_->mark_open_orders_dirty_();
    ++owner_->state_version_;
}
