#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::PositionSide;
using QTrading::dto::Order;
using QTrading::dto::Position;

namespace {

bool OrderOpensInHedgeMode(const Order& o)
{
    const bool is_buy = (o.side == QTrading::Dto::Trading::OrderSide::Buy);
    // Long opens with BUY, Short opens with SELL.
    if ((o.position_side == PositionSide::Long && is_buy) ||
        (o.position_side == PositionSide::Short && !is_buy)) {
        return true;
    }
    return false;
}

bool OrderReservesOpenMargin(const Order& o)
{
    // Do not reserve for explicit closing orders or reduce-only orders.
    if (o.closing_position_id >= 0) return false;
    if (o.reduce_only) return false;

    // One-way orders use PositionSide::Both.
    if (o.position_side == PositionSide::Both) {
        // With flip splitting, any remaining opening order can increase exposure.
        return true;
    }

    // Hedge: reserve only for opening-direction orders.
    return OrderOpensInHedgeMode(o);
}

QTrading::Dto::Account::BalanceSnapshot BuildPerpSnapshot(
    double perp_wallet_balance,
    const std::vector<Position>& positions,
    const std::vector<Order>& open_orders,
    const std::unordered_map<std::string, double>& symbol_leverage,
    const std::unordered_map<std::string, size_t>& symbol_id_by_name,
    const std::vector<double>& last_mark_price_by_id,
    double market_slippage_buffer)
{
    QTrading::Dto::Account::BalanceSnapshot s;
    s.WalletBalance = perp_wallet_balance;

    double unreal = 0.0;
    double posInit = 0.0;
    double maint = 0.0;
    for (const auto& p : positions) {
        if (p.instrument_type != InstrumentType::Perp) {
            continue;
        }
        unreal += p.unrealized_pnl;
        posInit += p.initial_margin;
        maint += p.maintenance_margin;
    }

    s.UnrealizedPnl = unreal;
    s.MarginBalance = s.WalletBalance + s.UnrealizedPnl;

    s.PositionInitialMargin = posInit;
    s.MaintenanceMargin = maint;

    auto estimate_notional = [&](const Order& o) -> double {
        if (o.quantity <= 0.0) return 0.0;
        if (o.price > 0.0) return o.quantity * o.price;
        auto it = symbol_id_by_name.find(o.symbol);
        if (it == symbol_id_by_name.end()) return 0.0;
        const size_t sym_id = it->second;
        if (sym_id >= last_mark_price_by_id.size()) return 0.0;
        const double mark = last_mark_price_by_id[sym_id];
        if (!std::isfinite(mark) || mark <= 0.0) return 0.0;
        return o.quantity * mark * (1.0 + std::max(0.0, market_slippage_buffer));
    };

    auto get_lev = [&](const Order& o) -> double {
        auto it = symbol_leverage.find(o.symbol);
        double lev = (it != symbol_leverage.end()) ? it->second : 1.0;
        return std::max(1.0, lev);
    };

    // Exposure-based open order initial margin:
    // - closing orders: no
    // - reduce_only: no (can't increase exposure)
    // - one-way: only orders that would increase exposure reserve margin
    // - hedge: only opening-direction orders reserve margin
    double openOrdInit = 0.0;
    for (const auto& o : open_orders) {
        if (o.instrument_type != InstrumentType::Perp) {
            continue;
        }

        if (o.one_way_reverse && o.closing_position_id >= 0) {
            double referenced_qty = 0.0;
            bool has_referenced_pos = false;
            for (const auto& p : positions) {
                if (p.id != o.closing_position_id ||
                    p.instrument_type != InstrumentType::Perp ||
                    p.quantity <= 0.0) {
                    continue;
                }
                referenced_qty = p.quantity;
                has_referenced_pos = true;
                break;
            }

            const double opening_qty = has_referenced_pos
                ? std::max(0.0, o.quantity - referenced_qty)
                : std::max(0.0, o.quantity);
            if (opening_qty <= 0.0) {
                continue;
            }

            Order opening_leg = o;
            opening_leg.quantity = opening_qty;
            opening_leg.closing_position_id = -1;
            opening_leg.one_way_reverse = false;
            const double notional = estimate_notional(opening_leg);
            if (notional <= 0.0) {
                continue;
            }
            openOrdInit += notional / get_lev(o);
            continue;
        }

        if (!OrderReservesOpenMargin(o)) continue;

        const double notional = estimate_notional(o);
        if (notional <= 0.0) continue;
        openOrdInit += notional / get_lev(o);
    }

    s.OpenOrderInitialMargin = openOrdInit;

    s.AvailableBalance = s.MarginBalance - s.PositionInitialMargin - s.OpenOrderInitialMargin;
    s.Equity = s.MarginBalance;
    return s;
}

} // namespace

QTrading::Dto::Account::BalanceSnapshot Account::get_balance() const
{
    return get_perp_balance();
}

QTrading::Dto::Account::BalanceSnapshot Account::get_perp_balance() const
{
    if (balance_cache_version_ != balance_version_) {
        balance_cache_ = BuildPerpSnapshot(
            perp_ledger_.wallet_balance(),
            positions_,
            open_orders_,
            symbol_leverage_,
            symbol_id_by_name_,
            last_mark_price_by_id_,
            market_slippage_buffer_);
        balance_cache_version_ = balance_version_;
    }
    return balance_cache_;
}

double Account::get_wallet_balance() const
{
    return perp_ledger_.wallet_balance();
}

double Account::get_margin_balance() const
{
    return get_perp_balance().MarginBalance;
}

double Account::get_available_balance() const
{
    return get_perp_balance().AvailableBalance;
}

double Account::total_unrealized_pnl() const
{
    return get_perp_balance().UnrealizedPnl;
}

double Account::get_equity() const
{
    return get_perp_balance().MarginBalance;
}

std::vector<Account::FundingApplyResult> Account::apply_funding(
    const std::string& symbol, uint64_t funding_time, double rate, double mark_price)
{
    return FundingService::ApplyFunding(*this, symbol, funding_time, rate, mark_price);
}

std::vector<Account::FundingApplyResult> Account::FundingService::ApplyFunding(
    Account& owner, const std::string& symbol, uint64_t funding_time, double rate, double mark_price)
{
    (void)funding_time;
    std::vector<FundingApplyResult> results;
    if (!owner.resolve_instrument_spec_(symbol).funding_enabled) {
        return results;
    }
    if (!std::isfinite(mark_price) || mark_price <= 0.0) {
        return results;
    }

    double total_delta = 0.0;
    auto it = owner.position_indices_by_symbol_.find(symbol);
    if (it == owner.position_indices_by_symbol_.end()) {
        return results;
    }

    const auto& indices = it->second;
    for (size_t idx : indices) {
        if (idx >= owner.positions_.size()) {
            continue;
        }
        auto& pos = owner.positions_[idx];
        if (pos.symbol != symbol || pos.quantity <= 0.0 || pos.instrument_type != InstrumentType::Perp) {
            continue;
        }

        const double notional = std::abs(pos.quantity) * mark_price;
        if (notional <= 0.0) {
            continue;
        }

        const double dir = pos.is_long ? -1.0 : 1.0;
        const double funding = notional * rate * dir;
        total_delta += funding;
        results.push_back(FundingApplyResult{ pos.id, pos.is_long, pos.quantity, funding });
    }

    if (results.empty()) {
        return results;
    }

    owner.perp_ledger_.credit_wallet(total_delta);
    owner.mark_balance_dirty_();
    ++owner.state_version_;
    return results;
}
