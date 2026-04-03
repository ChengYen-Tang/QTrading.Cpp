#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/Domain/MaintenanceMarginModel.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr double kEpsilon = 1e-12;

bool spot_buy_fee_paid_in_base(const State::BinanceExchangeRuntimeState& runtime_state)
{
    return runtime_state.simulation_config.spot_commission_mode ==
        Config::SpotCommissionMode::BaseOnBuyQuoteOnSell;
}

double fee_rate_for_fill(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const MatchFill& fill)
{
    if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
        const auto symbol_it = runtime_state.spot_symbol_fee_overrides.find(fill.symbol);
        if (symbol_it != runtime_state.spot_symbol_fee_overrides.end()) {
            return fill.is_taker ? symbol_it->second.taker_fee_rate : symbol_it->second.maker_fee_rate;
        }
        auto spot_it = ::spot_vip_fee_rates.find(runtime_state.vip_level);
        if (spot_it == ::spot_vip_fee_rates.end()) {
            spot_it = ::spot_vip_fee_rates.find(0);
        }
        return fill.is_taker ? spot_it->second.taker_fee_rate : spot_it->second.maker_fee_rate;
    }
    const auto symbol_it = runtime_state.perp_symbol_fee_overrides.find(fill.symbol);
    if (symbol_it != runtime_state.perp_symbol_fee_overrides.end()) {
        return fill.is_taker ? symbol_it->second.taker_fee_rate : symbol_it->second.maker_fee_rate;
    }
    auto perp_it = ::vip_fee_rates.find(runtime_state.vip_level);
    if (perp_it == ::vip_fee_rates.end()) {
        perp_it = ::vip_fee_rates.find(0);
    }
    return fill.is_taker ? perp_it->second.taker_fee_rate : perp_it->second.maker_fee_rate;
}

double resolve_symbol_leverage(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol) noexcept
{
    const auto it = runtime_state.symbol_leverage.find(symbol);
    if (it == runtime_state.symbol_leverage.end() || !(it->second > 0.0)) {
        return 1.0;
    }
    return it->second;
}

double compute_perp_realized_pnl_for_close(
    const QTrading::dto::Position& position,
    double close_quantity,
    double close_price) noexcept
{
    if (!(close_quantity > 0.0) || !(close_price > 0.0)) {
        return 0.0;
    }
    const double direction = position.is_long ? 1.0 : -1.0;
    return (close_price - position.entry_price) * close_quantity * direction;
}

void refresh_perp_position_risk_fields(
    const State::BinanceExchangeRuntimeState& runtime_state,
    QTrading::dto::Position& position,
    const State::StepKernelState* step_state,
    std::optional<size_t> symbol_id) noexcept
{
    const double leverage = resolve_symbol_leverage(runtime_state, position.symbol);
    const double notional = std::max(0.0, position.quantity * position.entry_price);
    position.leverage = leverage;
    position.notional = notional;
    position.initial_margin = notional / leverage;
    if (step_state != nullptr && symbol_id.has_value()) {
        position.maintenance_margin = ComputeMaintenanceMarginForSymbol(notional, *step_state, *symbol_id);
        return;
    }
    position.maintenance_margin = ComputeMaintenanceMargin(notional);
}

void seed_next_position_id_if_needed(State::BinanceExchangeRuntimeState& runtime_state)
{
    if (runtime_state.next_position_id != 0) {
        return;
    }
    int max_id = 0;
    for (const auto& position : runtime_state.positions) {
        if (position.id > max_id) {
            max_id = position.id;
        }
    }
    runtime_state.next_position_id = static_cast<uint64_t>(max_id) + 1U;
}

int allocate_next_position_id(State::BinanceExchangeRuntimeState& runtime_state)
{
    seed_next_position_id_if_needed(runtime_state);
    const uint64_t raw_id = runtime_state.next_position_id++;
    if (raw_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(raw_id);
}

size_t ensure_symbol_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    const auto it = runtime_state.position_symbol_to_id.find(symbol);
    if (it != runtime_state.position_symbol_to_id.end()) {
        return it->second;
    }
    const size_t id = runtime_state.position_symbol_to_id.size();
    runtime_state.position_symbol_to_id.emplace(symbol, id);
    return id;
}

std::optional<size_t> try_get_symbol_id(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    const auto it = runtime_state.position_symbol_to_id.find(symbol);
    if (it == runtime_state.position_symbol_to_id.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<size_t> resolve_fill_symbol_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    const MatchFill& fill)
{
    if (fill.symbol_id != std::numeric_limits<size_t>::max()) {
        return fill.symbol_id;
    }
    return try_get_symbol_id(runtime_state, fill.symbol);
}

State::PositionIndexKey make_position_key(
    size_t symbol_id,
    QTrading::Dto::Trading::InstrumentType instrument_type,
    bool is_long) noexcept
{
    State::PositionIndexKey key{};
    key.symbol_id = symbol_id;
    key.instrument_type = instrument_type;
    key.is_long = is_long;
    return key;
}

bool position_matches_key(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const QTrading::dto::Position& position,
    const State::PositionIndexKey& key)
{
    auto cache_it = runtime_state.position_symbol_id_by_position_id.find(position.id);
    std::optional<size_t> symbol_id = std::nullopt;
    if (cache_it != runtime_state.position_symbol_id_by_position_id.end()) {
        symbol_id = cache_it->second;
    }
    else {
        symbol_id = try_get_symbol_id(runtime_state, position.symbol);
    }
    return symbol_id.has_value() &&
        *symbol_id == key.symbol_id &&
        position.instrument_type == key.instrument_type &&
        position.is_long == key.is_long;
}

void enqueue_position_for_lookup(
    State::BinanceExchangeRuntimeState& runtime_state,
    const QTrading::dto::Position& position)
{
    size_t symbol_id = ensure_symbol_id(runtime_state, position.symbol);
    auto cached_it = runtime_state.position_symbol_id_by_position_id.find(position.id);
    if (cached_it != runtime_state.position_symbol_id_by_position_id.end()) {
        symbol_id = cached_it->second;
    }
    else {
        runtime_state.position_symbol_id_by_position_id[position.id] = symbol_id;
    }
    const auto key = make_position_key(symbol_id, position.instrument_type, position.is_long);
    runtime_state.position_ids_by_key[key].push_back(position.id);
}

void rebuild_position_index(State::BinanceExchangeRuntimeState& runtime_state)
{
    runtime_state.position_slot_by_id.clear();
    runtime_state.position_ids_by_key.clear();
    runtime_state.position_symbol_id_by_position_id.clear();
    runtime_state.position_symbol_id_by_slot.resize(runtime_state.positions.size(), std::numeric_limits<size_t>::max());
    for (size_t i = 0; i < runtime_state.positions.size(); ++i) {
        auto& position = runtime_state.positions[i];
        runtime_state.position_slot_by_id[position.id] = i;
        const size_t symbol_id = ensure_symbol_id(runtime_state, position.symbol);
        runtime_state.position_symbol_id_by_slot[i] = symbol_id;
        runtime_state.position_symbol_id_by_position_id[position.id] = symbol_id;
        enqueue_position_for_lookup(runtime_state, position);
    }
    runtime_state.position_index_ready = true;
}

void ensure_position_index(State::BinanceExchangeRuntimeState& runtime_state)
{
    if (!runtime_state.position_index_ready) {
        rebuild_position_index(runtime_state);
    }
}

std::optional<size_t> find_position_slot_by_key(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::PositionIndexKey& key)
{
    ensure_position_index(runtime_state);
    auto key_it = runtime_state.position_ids_by_key.find(key);
    if (key_it == runtime_state.position_ids_by_key.end()) {
        return std::nullopt;
    }

    auto& queue = key_it->second;
    while (!queue.empty()) {
        const int position_id = queue.front();
        const auto slot_it = runtime_state.position_slot_by_id.find(position_id);
        if (slot_it == runtime_state.position_slot_by_id.end()) {
            queue.pop_front();
            continue;
        }
        const size_t slot = slot_it->second;
        if (slot >= runtime_state.positions.size()) {
            queue.pop_front();
            continue;
        }
        const auto& position = runtime_state.positions[slot];
        if (position.id != position_id || !position_matches_key(runtime_state, position, key)) {
            queue.pop_front();
            continue;
        }
        return slot;
    }
    return std::nullopt;
}

QTrading::dto::Position* find_spot_inventory_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    const auto symbol_id = try_get_symbol_id(runtime_state, symbol);
    if (!symbol_id.has_value()) {
        return nullptr;
    }
    const auto key = make_position_key(
        *symbol_id,
        QTrading::Dto::Trading::InstrumentType::Spot,
        true);
    const auto slot = find_position_slot_by_key(runtime_state, key);
    if (!slot.has_value()) {
        return nullptr;
    }
    return &runtime_state.positions[*slot];
}

QTrading::dto::Position* find_spot_inventory_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t symbol_id)
{
    const auto key = make_position_key(
        symbol_id,
        QTrading::Dto::Trading::InstrumentType::Spot,
        true);
    const auto slot = find_position_slot_by_key(runtime_state, key);
    if (!slot.has_value()) {
        return nullptr;
    }
    return &runtime_state.positions[*slot];
}

QTrading::dto::Position* find_perp_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol,
    std::optional<bool> is_long = std::nullopt)
{
    const auto symbol_id = try_get_symbol_id(runtime_state, symbol);
    if (!symbol_id.has_value()) {
        return nullptr;
    }
    if (is_long.has_value()) {
        const auto key = make_position_key(
            *symbol_id,
            QTrading::Dto::Trading::InstrumentType::Perp,
            *is_long);
        const auto slot = find_position_slot_by_key(runtime_state, key);
        if (!slot.has_value()) {
            return nullptr;
        }
        return &runtime_state.positions[*slot];
    }

    const auto long_key = make_position_key(
        *symbol_id,
        QTrading::Dto::Trading::InstrumentType::Perp,
        true);
    const auto short_key = make_position_key(
        *symbol_id,
        QTrading::Dto::Trading::InstrumentType::Perp,
        false);
    const auto long_slot = find_position_slot_by_key(runtime_state, long_key);
    const auto short_slot = find_position_slot_by_key(runtime_state, short_key);
    if (!long_slot.has_value()) {
        if (!short_slot.has_value()) {
            return nullptr;
        }
        return &runtime_state.positions[*short_slot];
    }
    if (!short_slot.has_value()) {
        return &runtime_state.positions[*long_slot];
    }
    const size_t chosen = *long_slot < *short_slot ? *long_slot : *short_slot;
    return &runtime_state.positions[chosen];
}

QTrading::dto::Position* find_perp_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t symbol_id,
    std::optional<bool> is_long = std::nullopt)
{
    if (is_long.has_value()) {
        const auto key = make_position_key(
            symbol_id,
            QTrading::Dto::Trading::InstrumentType::Perp,
            *is_long);
        const auto slot = find_position_slot_by_key(runtime_state, key);
        if (!slot.has_value()) {
            return nullptr;
        }
        return &runtime_state.positions[*slot];
    }

    const auto long_key = make_position_key(
        symbol_id,
        QTrading::Dto::Trading::InstrumentType::Perp,
        true);
    const auto short_key = make_position_key(
        symbol_id,
        QTrading::Dto::Trading::InstrumentType::Perp,
        false);
    const auto long_slot = find_position_slot_by_key(runtime_state, long_key);
    const auto short_slot = find_position_slot_by_key(runtime_state, short_key);
    if (!long_slot.has_value()) {
        if (!short_slot.has_value()) {
            return nullptr;
        }
        return &runtime_state.positions[*short_slot];
    }
    if (!short_slot.has_value()) {
        return &runtime_state.positions[*long_slot];
    }
    const size_t chosen = *long_slot < *short_slot ? *long_slot : *short_slot;
    return &runtime_state.positions[chosen];
}

void index_appended_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t slot)
{
    if (slot >= runtime_state.positions.size()) {
        return;
    }
    auto& position = runtime_state.positions[slot];
    if (runtime_state.position_symbol_id_by_slot.size() <= slot) {
        runtime_state.position_symbol_id_by_slot.resize(slot + 1, std::numeric_limits<size_t>::max());
    }
    if (runtime_state.position_symbol_id_by_slot[slot] == std::numeric_limits<size_t>::max()) {
        runtime_state.position_symbol_id_by_slot[slot] = ensure_symbol_id(runtime_state, position.symbol);
    }
    runtime_state.position_symbol_id_by_position_id[position.id] = runtime_state.position_symbol_id_by_slot[slot];
    runtime_state.position_slot_by_id[position.id] = slot;
    enqueue_position_for_lookup(runtime_state, position);
}

bool remove_position_by_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    int position_id)
{
    ensure_position_index(runtime_state);
    const auto it = runtime_state.position_slot_by_id.find(position_id);
    if (it == runtime_state.position_slot_by_id.end()) {
        return false;
    }

    const size_t remove_slot = it->second;
    const size_t last_slot = runtime_state.positions.size() - 1;
    if (remove_slot != last_slot) {
        std::swap(runtime_state.positions[remove_slot], runtime_state.positions[last_slot]);
        if (remove_slot < runtime_state.position_symbol_id_by_slot.size() &&
            last_slot < runtime_state.position_symbol_id_by_slot.size()) {
            std::swap(runtime_state.position_symbol_id_by_slot[remove_slot], runtime_state.position_symbol_id_by_slot[last_slot]);
        }
        runtime_state.position_slot_by_id[runtime_state.positions[remove_slot].id] = remove_slot;
        if (remove_slot < runtime_state.position_symbol_id_by_slot.size()) {
            runtime_state.position_symbol_id_by_position_id[runtime_state.positions[remove_slot].id] =
                runtime_state.position_symbol_id_by_slot[remove_slot];
        }
    }
    runtime_state.positions.pop_back();
    if (!runtime_state.position_symbol_id_by_slot.empty()) {
        runtime_state.position_symbol_id_by_slot.pop_back();
    }
    runtime_state.position_slot_by_id.erase(position_id);
    runtime_state.position_symbol_id_by_position_id.erase(position_id);
    return true;
}

void append_perp_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    const MatchFill& fill,
    double quantity,
    bool is_long,
    double fee,
    double fee_rate)
{
    QTrading::dto::Position created{};
    created.id = allocate_next_position_id(runtime_state);
    created.order_id = fill.order_id;
    created.symbol = fill.symbol;
    created.quantity = quantity;
    created.entry_price = fill.price;
    created.is_long = is_long;
    created.notional = created.quantity * created.entry_price;
    created.initial_margin = 0.0;
    created.maintenance_margin = 0.0;
    created.fee = fee;
    created.leverage = 1.0;
    created.fee_rate = fee_rate;
    created.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    refresh_perp_position_risk_fields(runtime_state, created, step_state, fill.symbol_id);
    runtime_state.positions.emplace_back(std::move(created));
    if (const auto symbol_id = resolve_fill_symbol_id(runtime_state, fill); symbol_id.has_value()) {
        runtime_state.position_symbol_id_by_slot.push_back(*symbol_id);
        runtime_state.position_symbol_id_by_position_id[runtime_state.positions.back().id] = *symbol_id;
    }
    else {
        runtime_state.position_symbol_id_by_slot.push_back(std::numeric_limits<size_t>::max());
    }
    index_appended_position(runtime_state, runtime_state.positions.size() - 1);
}

bool apply_spot_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const MatchFill& fill)
{
    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(runtime_state, fill);
    const double fee = notional * fee_rate;
    const bool base_fee_on_buy = spot_buy_fee_paid_in_base(runtime_state) &&
        fill.side == QTrading::Dto::Trading::OrderSide::Buy;
    const double quantity_fee = base_fee_on_buy ? fill.quantity * fee_rate : 0.0;
    const double net_quantity = fill.quantity - quantity_fee;
    const auto fill_symbol_id = resolve_fill_symbol_id(runtime_state, fill);

    if (fill.side == QTrading::Dto::Trading::OrderSide::Buy) {
        account.apply_spot_cash_delta(-(notional + (base_fee_on_buy ? 0.0 : fee)));

        auto* position = fill_symbol_id.has_value()
            ? find_spot_inventory_position(runtime_state, *fill_symbol_id)
            : find_spot_inventory_position(runtime_state, fill.symbol);
        if (!position) {
            QTrading::dto::Position created{};
            created.id = allocate_next_position_id(runtime_state);
            created.order_id = fill.order_id;
            created.symbol = fill.symbol;
            created.quantity = net_quantity;
            created.entry_price = fill.price;
            created.is_long = true;
            created.notional = created.quantity * created.entry_price;
            created.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
            runtime_state.positions.emplace_back(std::move(created));
            if (const auto symbol_id = resolve_fill_symbol_id(runtime_state, fill); symbol_id.has_value()) {
                runtime_state.position_symbol_id_by_slot.push_back(*symbol_id);
                runtime_state.position_symbol_id_by_position_id[runtime_state.positions.back().id] = *symbol_id;
            }
            else {
                runtime_state.position_symbol_id_by_slot.push_back(std::numeric_limits<size_t>::max());
            }
            index_appended_position(runtime_state, runtime_state.positions.size() - 1);
            return true;
        }

        const double before = position->quantity;
        const double after = before + net_quantity;
        if (after > kEpsilon) {
            position->entry_price = ((position->entry_price * before) + (fill.price * net_quantity)) / after;
        }
        position->quantity = after;
        position->notional = after * position->entry_price;
        position->fee += fee;
        position->fee_rate = fee_rate;
        return true;
    }

    account.apply_spot_cash_delta(notional - fee);
    auto* position = fill_symbol_id.has_value()
        ? find_spot_inventory_position(runtime_state, *fill_symbol_id)
        : find_spot_inventory_position(runtime_state, fill.symbol);
    if (!position) {
        return false;
    }
    const double before_quantity = position->quantity;
    position->quantity = std::max(0.0, position->quantity - fill.quantity);
    position->notional = position->quantity * position->entry_price;
    if (position->quantity <= kEpsilon) {
        const int closed_id = position->id;
        return remove_position_by_id(runtime_state, closed_id);
    }
    return std::abs(position->quantity - before_quantity) > kEpsilon;
}

bool apply_perp_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState* step_state,
    const MatchFill& fill)
{
    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(runtime_state, fill);
    const double fee = notional * fee_rate;
    double signed_fill = fill.side == QTrading::Dto::Trading::OrderSide::Buy ? fill.quantity : -fill.quantity;
    const auto fill_symbol_id = resolve_fill_symbol_id(runtime_state, fill);
    if (runtime_state.hedge_mode) {
        const bool target_is_long = fill.position_side == QTrading::Dto::Trading::PositionSide::Long;
        auto* position = fill_symbol_id.has_value()
            ? find_perp_position(runtime_state, *fill_symbol_id, target_is_long)
            : find_perp_position(runtime_state, fill.symbol, target_is_long);
        if (fill.reduce_only || fill.close_position) {
            if (!position) {
                return false;
            }
            const auto closes_side = target_is_long
                ? QTrading::Dto::Trading::OrderSide::Sell
                : QTrading::Dto::Trading::OrderSide::Buy;
            if (fill.side != closes_side) {
                return false;
            }
            const double effective = std::min(position->quantity, fill.quantity);
            if (effective <= kEpsilon) {
                return false;
            }
            const double realized_pnl = compute_perp_realized_pnl_for_close(*position, effective, fill.price);
            account.apply_perp_wallet_delta(realized_pnl - fee);
            const double before_quantity = position->quantity;
            position->quantity = std::max(0.0, position->quantity - effective);
            if (position->quantity <= kEpsilon) {
                const int closed_id = position->id;
                return remove_position_by_id(runtime_state, closed_id);
            }
            refresh_perp_position_risk_fields(runtime_state, *position, step_state, fill_symbol_id);
            return std::abs(position->quantity - before_quantity) > kEpsilon;
        }

        account.apply_perp_wallet_delta(-fee);
        if (!position || !runtime_state.merge_positions_enabled) {
            append_perp_position(
                runtime_state,
                step_state,
                fill,
                fill.quantity,
                fill.side == QTrading::Dto::Trading::OrderSide::Buy,
                fee,
                fee_rate);
            return true;
        }

        const double before = position->quantity;
        const double after = before + fill.quantity;
        if (after > kEpsilon) {
            position->entry_price = ((position->entry_price * before) + (fill.price * fill.quantity)) / after;
        }
        position->quantity = after;
        position->is_long = target_is_long;
        position->fee += fee;
        position->fee_rate = fee_rate;
        position->instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
        refresh_perp_position_risk_fields(runtime_state, *position, step_state, fill_symbol_id);
        return true;
    }

    auto* position = fill_symbol_id.has_value()
        ? find_perp_position(runtime_state, *fill_symbol_id)
        : find_perp_position(runtime_state, fill.symbol);
    if (fill.reduce_only) {
        if (!position) {
            return false;
        }
        const double current_signed = position->is_long ? position->quantity : -position->quantity;
        if (current_signed * signed_fill >= 0.0) {
            return false;
        }
        const double reducible = std::abs(current_signed);
        const double requested = std::abs(signed_fill);
        const double effective = std::min(reducible, requested);
        signed_fill = signed_fill > 0.0 ? effective : -effective;
        if (effective <= kEpsilon) {
            return false;
        }
    }
    double realized_pnl = 0.0;
    if (position) {
        const double current_signed = position->is_long ? position->quantity : -position->quantity;
        if (current_signed * signed_fill < 0.0) {
            const double close_quantity = std::min(std::abs(current_signed), std::abs(signed_fill));
            realized_pnl = compute_perp_realized_pnl_for_close(*position, close_quantity, fill.price);
        }
    }
    account.apply_perp_wallet_delta(realized_pnl - fee);

    if (!position || (runtime_state.merge_positions_enabled == false &&
            ((position->is_long && signed_fill > 0.0) || (!position->is_long && signed_fill < 0.0)))) {
        append_perp_position(
            runtime_state,
            step_state,
            fill,
            std::abs(signed_fill),
            signed_fill > 0.0,
            fee,
            fee_rate);
        return true;
    }

    const double current_signed = position->is_long ? position->quantity : -position->quantity;
    const double next_signed = current_signed + signed_fill;
    if (std::abs(next_signed) <= kEpsilon) {
        const int closed_id = position->id;
        return remove_position_by_id(runtime_state, closed_id);
    }

    if ((current_signed > 0.0 && signed_fill > 0.0) || (current_signed < 0.0 && signed_fill < 0.0)) {
        const double before = std::abs(current_signed);
        const double after = std::abs(next_signed);
        position->entry_price = ((position->entry_price * before) + (fill.price * std::abs(signed_fill))) / after;
    }
    else if ((current_signed > 0.0 && next_signed < 0.0) || (current_signed < 0.0 && next_signed > 0.0)) {
        position->entry_price = fill.price;
    }

    position->quantity = std::abs(next_signed);
    position->is_long = next_signed > 0.0;
    position->fee += fee;
    position->fee_rate = fee_rate;
    position->instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    refresh_perp_position_risk_fields(runtime_state, *position, step_state, fill_symbol_id);
    enqueue_position_for_lookup(runtime_state, *position);
    return true;
}

void apply_impl(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState* step_state,
    const std::vector<MatchFill>& fills)
{
    rebuild_position_index(runtime_state);
    bool positions_mutated = false;
    for (const auto& fill : fills) {
        if (fill.quantity <= kEpsilon || fill.price <= 0.0) {
            continue;
        }
        if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
            positions_mutated = apply_spot_fill(runtime_state, account, fill) || positions_mutated;
            continue;
        }
        positions_mutated = apply_perp_fill(runtime_state, account, step_state, fill) || positions_mutated;
    }
    if (positions_mutated) {
        ++runtime_state.positions_version;
    }
}

} // namespace

void FillSettlementEngine::Apply(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const std::vector<MatchFill>& fills)
{
    apply_impl(runtime_state, account, nullptr, fills);
}

void FillSettlementEngine::Apply(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState& step_state,
    const std::vector<MatchFill>& fills)
{
    apply_impl(runtime_state, account, &step_state, fills);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain

