#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
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
constexpr size_t kInvalidSymbolId = std::numeric_limits<size_t>::max();

[[noreturn]] void throw_invariant_breach(const std::string& message)
{
    throw std::logic_error("FillSettlementEngine invariant breach: " + message);
}

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

bool is_valid_authoritative_symbol_id(
    const State::StepKernelState* step_state,
    size_t symbol_id) noexcept
{
    if (symbol_id == kInvalidSymbolId) {
        return false;
    }
    if (step_state == nullptr) {
        return true;
    }
    return symbol_id < step_state->symbols.size();
}

std::optional<size_t> try_step_symbol_id(
    const State::StepKernelState* step_state,
    const std::string& symbol)
{
    if (step_state == nullptr) {
        return std::nullopt;
    }
    const auto it = step_state->symbol_to_id.find(symbol);
    if (it == step_state->symbol_to_id.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<size_t> try_cached_position_symbol_id(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const QTrading::dto::Position& position,
    std::optional<size_t> slot = std::nullopt)
{
    if (slot.has_value() &&
        *slot < runtime_state.position_symbol_id_by_slot.size() &&
        runtime_state.position_symbol_id_by_slot[*slot] != kInvalidSymbolId) {
        return runtime_state.position_symbol_id_by_slot[*slot];
    }
    const auto cached_it = runtime_state.position_symbol_id_by_position_id.find(position.id);
    if (cached_it != runtime_state.position_symbol_id_by_position_id.end() &&
        cached_it->second != kInvalidSymbolId) {
        return cached_it->second;
    }
    return std::nullopt;
}

std::optional<size_t> try_resolve_fill_symbol_id(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    const MatchFill& fill)
{
    const auto step_symbol_id = try_step_symbol_id(step_state, fill.symbol);
    if (fill.symbol_id != kInvalidSymbolId) {
        if (step_symbol_id.has_value() && *step_symbol_id != fill.symbol_id) {
            return std::nullopt;
        }
        if (!is_valid_authoritative_symbol_id(step_state, fill.symbol_id)) {
            return std::nullopt;
        }
        return fill.symbol_id;
    }
    if (step_symbol_id.has_value()) {
        return step_symbol_id;
    }
    if (const auto order_it = runtime_state.order_symbol_id_by_order_id.find(fill.order_id);
        order_it != runtime_state.order_symbol_id_by_order_id.end() &&
        order_it->second != kInvalidSymbolId) {
        return order_it->second;
    }
    return std::nullopt;
}

void invalidate_visible_positions_cache(State::BinanceExchangeRuntimeState& runtime_state)
{
    runtime_state.visible_positions_cache_version = std::numeric_limits<uint64_t>::max();
}

void ensure_spot_inventory_shape(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    size_t symbol_id)
{
    size_t required = 0;
    if (step_state != nullptr) {
        required = step_state->symbols.size();
    }
    if (symbol_id != kInvalidSymbolId) {
        required = std::max(required, symbol_id + 1);
    }
    if (required == 0) {
        return;
    }
    if (runtime_state.spot_inventory_qty_by_symbol.size() < required) {
        runtime_state.spot_inventory_qty_by_symbol.resize(required, 0.0);
        runtime_state.spot_inventory_entry_price_by_symbol.resize(required, 0.0);
        runtime_state.spot_inventory_position_id_by_symbol.resize(required, 0);
    }
}

void normalize_legacy_spot_inventory(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state)
{
    if (runtime_state.positions.empty()) {
        return;
    }

    std::vector<QTrading::dto::Position> perp_positions;
    perp_positions.reserve(runtime_state.positions.size());
    runtime_state.position_symbol_id_by_slot.clear();

    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot) {
            perp_positions.push_back(position);
            continue;
        }
        const auto symbol_id = try_step_symbol_id(step_state, position.symbol);
        if (!symbol_id.has_value()) {
            throw_invariant_breach("legacy spot inventory missing authoritative symbol id for " + position.symbol);
        }
        ensure_spot_inventory_shape(runtime_state, step_state, *symbol_id);
        runtime_state.spot_inventory_qty_by_symbol[*symbol_id] = position.quantity;
        runtime_state.spot_inventory_entry_price_by_symbol[*symbol_id] = position.entry_price;
        runtime_state.spot_inventory_position_id_by_symbol[*symbol_id] = position.id;
    }

    if (perp_positions.size() != runtime_state.positions.size()) {
        runtime_state.positions = std::move(perp_positions);
        runtime_state.position_index_ready = false;
        invalidate_visible_positions_cache(runtime_state);
    }
}

std::optional<size_t> resolve_position_symbol_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    const QTrading::dto::Position& position,
    size_t slot)
{
    const auto step_symbol_id = try_step_symbol_id(step_state, position.symbol);
    const auto cached_symbol_id = try_cached_position_symbol_id(runtime_state, position, slot);
    if (step_symbol_id.has_value() && cached_symbol_id.has_value() && *step_symbol_id != *cached_symbol_id) {
        return std::nullopt;
    }

    const auto resolved = step_symbol_id.has_value() ? step_symbol_id : cached_symbol_id;
    if (!resolved.has_value() || !is_valid_authoritative_symbol_id(step_state, *resolved)) {
        return std::nullopt;
    }

    if (runtime_state.position_symbol_id_by_slot.size() <= slot) {
        runtime_state.position_symbol_id_by_slot.resize(slot + 1, kInvalidSymbolId);
    }
    runtime_state.position_symbol_id_by_slot[slot] = *resolved;
    runtime_state.position_symbol_id_by_position_id[position.id] = *resolved;
    return resolved;
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

void reset_position_index(State::BinanceExchangeRuntimeState& runtime_state)
{
    runtime_state.position_slot_by_id.clear();
    runtime_state.position_ids_by_key.clear();
    runtime_state.position_symbol_id_by_position_id.clear();
    runtime_state.position_index_ready = false;
}

bool rebuild_position_index_from_cached_symbol_ids(State::BinanceExchangeRuntimeState& runtime_state)
{
    reset_position_index(runtime_state);
    if (runtime_state.position_symbol_id_by_slot.size() != runtime_state.positions.size()) {
        return false;
    }

    for (size_t slot = 0; slot < runtime_state.positions.size(); ++slot) {
        const auto& position = runtime_state.positions[slot];
        const size_t symbol_id = runtime_state.position_symbol_id_by_slot[slot];
        if (symbol_id == kInvalidSymbolId) {
            return false;
        }
        runtime_state.position_slot_by_id[position.id] = slot;
        runtime_state.position_symbol_id_by_position_id[position.id] = symbol_id;
        runtime_state.position_ids_by_key[make_position_key(symbol_id, position.instrument_type, position.is_long)]
            .push_back(position.id);
    }

    runtime_state.position_index_ready = true;
    return true;
}

bool initialize_position_index(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state)
{
    runtime_state.position_symbol_id_by_slot.resize(runtime_state.positions.size(), kInvalidSymbolId);
    for (size_t slot = 0; slot < runtime_state.positions.size(); ++slot) {
        if (!resolve_position_symbol_id(runtime_state, step_state, runtime_state.positions[slot], slot).has_value()) {
            reset_position_index(runtime_state);
            return false;
        }
    }
    return rebuild_position_index_from_cached_symbol_ids(runtime_state);
}

bool ensure_position_index(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state)
{
    if (runtime_state.position_index_ready) {
        return true;
    }
    return initialize_position_index(runtime_state, step_state);
}

std::optional<size_t> find_position_slot_by_key(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    const State::PositionIndexKey& key)
{
    if (!ensure_position_index(runtime_state, step_state)) {
        return std::nullopt;
    }

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
        if (slot >= runtime_state.positions.size() ||
            slot >= runtime_state.position_symbol_id_by_slot.size()) {
            queue.pop_front();
            continue;
        }
        const auto& position = runtime_state.positions[slot];
        if (position.id != position_id ||
            runtime_state.position_symbol_id_by_slot[slot] != key.symbol_id ||
            position.instrument_type != key.instrument_type ||
            position.is_long != key.is_long) {
            queue.pop_front();
            continue;
        }
        return slot;
    }
    return std::nullopt;
}

std::optional<size_t> find_position_slot_linear(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol,
    QTrading::Dto::Trading::InstrumentType instrument_type,
    std::optional<bool> is_long = std::nullopt)
{
    for (size_t slot = 0; slot < runtime_state.positions.size(); ++slot) {
        const auto& position = runtime_state.positions[slot];
        if (position.symbol != symbol || position.instrument_type != instrument_type) {
            continue;
        }
        if (is_long.has_value() && position.is_long != *is_long) {
            continue;
        }
        return slot;
    }
    return std::nullopt;
}

std::optional<size_t> find_spot_inventory_position_slot(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    size_t symbol_id,
    const std::string& symbol)
{
    if (symbol_id != kInvalidSymbolId) {
        const auto key = make_position_key(
            symbol_id,
            QTrading::Dto::Trading::InstrumentType::Spot,
            true);
        if (const auto slot = find_position_slot_by_key(runtime_state, step_state, key); slot.has_value()) {
            return slot;
        }
    }
    return find_position_slot_linear(
        runtime_state,
        symbol,
        QTrading::Dto::Trading::InstrumentType::Spot,
        true);
}

std::optional<size_t> find_perp_position_slot(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    size_t symbol_id,
    const std::string& symbol,
    std::optional<bool> is_long = std::nullopt)
{
    if (symbol_id != kInvalidSymbolId) {
        if (is_long.has_value()) {
            const auto key = make_position_key(
                symbol_id,
                QTrading::Dto::Trading::InstrumentType::Perp,
                *is_long);
            if (const auto slot = find_position_slot_by_key(runtime_state, step_state, key); slot.has_value()) {
                return slot;
            }
        }
        else {
            const auto long_slot = find_position_slot_by_key(
                runtime_state,
                step_state,
                make_position_key(symbol_id, QTrading::Dto::Trading::InstrumentType::Perp, true));
            const auto short_slot = find_position_slot_by_key(
                runtime_state,
                step_state,
                make_position_key(symbol_id, QTrading::Dto::Trading::InstrumentType::Perp, false));
            if (long_slot.has_value() && short_slot.has_value()) {
                return *long_slot < *short_slot ? long_slot : short_slot;
            }
            if (long_slot.has_value()) {
                return long_slot;
            }
            if (short_slot.has_value()) {
                return short_slot;
            }
        }
    }
    return find_position_slot_linear(
        runtime_state,
        symbol,
        QTrading::Dto::Trading::InstrumentType::Perp,
        is_long);
}

void register_position_slot(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t slot,
    size_t symbol_id)
{
    if (slot >= runtime_state.positions.size()) {
        throw_invariant_breach("register_position_slot out of range");
    }

    if (runtime_state.position_symbol_id_by_slot.size() <= slot) {
        runtime_state.position_symbol_id_by_slot.resize(slot + 1, kInvalidSymbolId);
    }

    auto& position = runtime_state.positions[slot];
    runtime_state.position_symbol_id_by_slot[slot] = symbol_id;
    runtime_state.position_symbol_id_by_position_id[position.id] = symbol_id;
    runtime_state.position_slot_by_id[position.id] = slot;
    runtime_state.position_ids_by_key[make_position_key(symbol_id, position.instrument_type, position.is_long)]
        .push_back(position.id);
    runtime_state.position_index_ready = true;
}

void reindex_position_slot(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t slot,
    size_t symbol_id)
{
    if (slot >= runtime_state.positions.size()) {
        throw_invariant_breach("reindex_position_slot out of range");
    }

    if (runtime_state.position_symbol_id_by_slot.size() <= slot) {
        runtime_state.position_symbol_id_by_slot.resize(slot + 1, kInvalidSymbolId);
    }

    const auto& position = runtime_state.positions[slot];
    runtime_state.position_symbol_id_by_slot[slot] = symbol_id;
    runtime_state.position_symbol_id_by_position_id[position.id] = symbol_id;
    runtime_state.position_slot_by_id[position.id] = slot;
    runtime_state.position_ids_by_key[make_position_key(symbol_id, position.instrument_type, position.is_long)]
        .push_back(position.id);
}

void append_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    QTrading::dto::Position&& position,
    size_t symbol_id)
{
    runtime_state.positions.emplace_back(std::move(position));
    register_position_slot(runtime_state, runtime_state.positions.size() - 1, symbol_id);
    invalidate_visible_positions_cache(runtime_state);
}

void erase_position_slot(
    State::BinanceExchangeRuntimeState& runtime_state,
    size_t remove_slot)
{
    if (remove_slot >= runtime_state.positions.size()) {
        throw_invariant_breach("erase_position_slot out of range");
    }

    const int removed_position_id = runtime_state.positions[remove_slot].id;
    const size_t last_slot = runtime_state.positions.size() - 1;
    if (remove_slot != last_slot) {
        std::swap(runtime_state.positions[remove_slot], runtime_state.positions[last_slot]);
        if (remove_slot < runtime_state.position_symbol_id_by_slot.size() &&
            last_slot < runtime_state.position_symbol_id_by_slot.size()) {
            std::swap(
                runtime_state.position_symbol_id_by_slot[remove_slot],
                runtime_state.position_symbol_id_by_slot[last_slot]);
        }
    }

    runtime_state.positions.pop_back();
    if (!runtime_state.position_symbol_id_by_slot.empty()) {
        runtime_state.position_symbol_id_by_slot.pop_back();
    }

    runtime_state.position_slot_by_id.erase(removed_position_id);
    runtime_state.position_symbol_id_by_position_id.erase(removed_position_id);

    if (remove_slot < runtime_state.positions.size()) {
        const auto& moved_position = runtime_state.positions[remove_slot];
        runtime_state.position_slot_by_id[moved_position.id] = remove_slot;
        if (remove_slot < runtime_state.position_symbol_id_by_slot.size()) {
            runtime_state.position_symbol_id_by_position_id[moved_position.id] =
                runtime_state.position_symbol_id_by_slot[remove_slot];
        }
    }

    if (runtime_state.positions.empty()) {
        reset_position_index(runtime_state);
        runtime_state.position_symbol_id_by_slot.clear();
    }
    invalidate_visible_positions_cache(runtime_state);
}

void append_perp_position(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state,
    const MatchFill& fill,
    size_t symbol_id,
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
    refresh_perp_position_risk_fields(runtime_state, created, step_state, symbol_id);
    append_position(runtime_state, std::move(created), symbol_id);
}

void assert_position_index_invariants(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState* step_state)
{
#ifndef NDEBUG
    if (runtime_state.positions.empty()) {
        assert(runtime_state.position_symbol_id_by_slot.empty());
        return;
    }

    assert(runtime_state.position_symbol_id_by_slot.size() == runtime_state.positions.size());
    assert(ensure_position_index(runtime_state, step_state));
    for (size_t slot = 0; slot < runtime_state.positions.size(); ++slot) {
        const auto& position = runtime_state.positions[slot];
        const size_t symbol_id = runtime_state.position_symbol_id_by_slot[slot];
        assert(symbol_id != kInvalidSymbolId);
        if (step_state != nullptr) {
            const auto it = step_state->symbol_to_id.find(position.symbol);
            assert(it != step_state->symbol_to_id.end());
            assert(it->second == symbol_id);
        }
        const auto slot_it = runtime_state.position_slot_by_id.find(position.id);
        assert(slot_it != runtime_state.position_slot_by_id.end());
        assert(slot_it->second == slot);
        const auto symbol_it = runtime_state.position_symbol_id_by_position_id.find(position.id);
        assert(symbol_it != runtime_state.position_symbol_id_by_position_id.end());
        assert(symbol_it->second == symbol_id);
    }
#else
    (void)runtime_state;
    (void)step_state;
#endif
}

bool apply_spot_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState* step_state,
    const MatchFill& fill)
{
    const auto fill_symbol_id = try_resolve_fill_symbol_id(runtime_state, step_state, fill);
    if (step_state != nullptr && !fill_symbol_id.has_value()) {
        throw_invariant_breach("spot fill missing authoritative symbol id for " + fill.symbol);
    }

    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(runtime_state, fill);
    const double fee = notional * fee_rate;
    const bool base_fee_on_buy = spot_buy_fee_paid_in_base(runtime_state) &&
        fill.side == QTrading::Dto::Trading::OrderSide::Buy;
    const double quantity_fee = base_fee_on_buy ? fill.quantity * fee_rate : 0.0;
    const double net_quantity = fill.quantity - quantity_fee;
    const size_t resolved_symbol_id = fill_symbol_id.value_or(kInvalidSymbolId);
    ensure_spot_inventory_shape(runtime_state, step_state, resolved_symbol_id);
    if (resolved_symbol_id == kInvalidSymbolId ||
        resolved_symbol_id >= runtime_state.spot_inventory_qty_by_symbol.size()) {
        throw_invariant_breach("spot fill cannot resolve inventory slot for " + fill.symbol);
    }

    auto& qty = runtime_state.spot_inventory_qty_by_symbol[resolved_symbol_id];
    auto& entry_price = runtime_state.spot_inventory_entry_price_by_symbol[resolved_symbol_id];
    auto& synthetic_position_id = runtime_state.spot_inventory_position_id_by_symbol[resolved_symbol_id];

    if (fill.side == QTrading::Dto::Trading::OrderSide::Buy) {
        account.apply_spot_cash_delta(-(notional + (base_fee_on_buy ? 0.0 : fee)));
        const double before = qty;
        const double after = before + net_quantity;
        if (!(after > kEpsilon)) {
            throw_invariant_breach("spot buy produced non-positive inventory for " + fill.symbol);
        }
        if (before > kEpsilon) {
            entry_price = ((entry_price * before) + (fill.price * net_quantity)) / after;
        }
        else {
            entry_price = fill.price;
            if (synthetic_position_id <= 0) {
                synthetic_position_id = allocate_next_position_id(runtime_state);
            }
        }
        qty = after;
        invalidate_visible_positions_cache(runtime_state);
        return true;
    }

    if (!(qty > kEpsilon)) {
        throw_invariant_breach("spot sell fill has no matching inventory position for " + fill.symbol);
    }
    if (qty + kEpsilon < fill.quantity) {
        throw_invariant_breach("spot sell fill exceeds inventory for " + fill.symbol);
    }

    const double next_quantity = std::max(0.0, qty - fill.quantity);
    if (next_quantity <= kEpsilon) {
        qty = 0.0;
        entry_price = 0.0;
        synthetic_position_id = 0;
    }
    else {
        qty = next_quantity;
    }
    account.apply_spot_cash_delta(notional - fee);
    invalidate_visible_positions_cache(runtime_state);
    return true;
}

bool apply_perp_fill(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState* step_state,
    const MatchFill& fill)
{
    const auto fill_symbol_id = try_resolve_fill_symbol_id(runtime_state, step_state, fill);
    if (step_state != nullptr && !fill_symbol_id.has_value()) {
        throw_invariant_breach("perp fill missing authoritative symbol id for " + fill.symbol);
    }

    const size_t resolved_symbol_id = fill_symbol_id.value_or(kInvalidSymbolId);
    const double notional = fill.quantity * fill.price;
    const double fee_rate = fee_rate_for_fill(runtime_state, fill);
    const double fee = notional * fee_rate;
    double signed_fill = fill.side == QTrading::Dto::Trading::OrderSide::Buy ? fill.quantity : -fill.quantity;

    if (runtime_state.hedge_mode) {
        const bool target_is_long = fill.position_side == QTrading::Dto::Trading::PositionSide::Long;
        const auto position_slot = find_perp_position_slot(
            runtime_state,
            step_state,
            resolved_symbol_id,
            fill.symbol,
            target_is_long);

        if (fill.reduce_only || fill.close_position) {
            if (!position_slot.has_value()) {
                throw_invariant_breach("hedge reduce fill has no matching perp position for " + fill.symbol);
            }

            auto& position = runtime_state.positions[*position_slot];
            const auto closes_side = target_is_long
                ? QTrading::Dto::Trading::OrderSide::Sell
                : QTrading::Dto::Trading::OrderSide::Buy;
            if (fill.side != closes_side) {
                throw_invariant_breach("hedge reduce fill side mismatch for " + fill.symbol);
            }

            const double effective = std::min(position.quantity, fill.quantity);
            if (effective <= kEpsilon) {
                throw_invariant_breach("hedge reduce fill has no reducible quantity for " + fill.symbol);
            }

            const double realized_pnl = compute_perp_realized_pnl_for_close(position, effective, fill.price);
            const double next_quantity = std::max(0.0, position.quantity - effective);
            if (next_quantity <= kEpsilon) {
                erase_position_slot(runtime_state, *position_slot);
            }
            else {
                position.quantity = next_quantity;
                refresh_perp_position_risk_fields(runtime_state, position, step_state, fill_symbol_id);
            }
            account.apply_perp_wallet_delta(realized_pnl - fee);
            return true;
        }

        if (!position_slot.has_value() || !runtime_state.merge_positions_enabled) {
            append_perp_position(
                runtime_state,
                step_state,
                fill,
                resolved_symbol_id,
                fill.quantity,
                fill.side == QTrading::Dto::Trading::OrderSide::Buy,
                fee,
                fee_rate);
            account.apply_perp_wallet_delta(-fee);
            return true;
        }

        auto& position = runtime_state.positions[*position_slot];
        const double before = position.quantity;
        const double after = before + fill.quantity;
        if (after > kEpsilon) {
            position.entry_price = ((position.entry_price * before) + (fill.price * fill.quantity)) / after;
        }
        position.quantity = after;
        position.is_long = target_is_long;
        position.fee += fee;
        position.fee_rate = fee_rate;
        position.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
        refresh_perp_position_risk_fields(runtime_state, position, step_state, fill_symbol_id);
        reindex_position_slot(runtime_state, *position_slot, resolved_symbol_id);
        account.apply_perp_wallet_delta(-fee);
        return true;
    }

    const auto position_slot = find_perp_position_slot(
        runtime_state,
        step_state,
        resolved_symbol_id,
        fill.symbol);
    if (fill.reduce_only) {
        if (!position_slot.has_value()) {
            throw_invariant_breach("reduce-only fill has no matching perp position for " + fill.symbol);
        }
        const auto& position = runtime_state.positions[*position_slot];
        const double current_signed = position.is_long ? position.quantity : -position.quantity;
        if (current_signed * signed_fill >= 0.0) {
            throw_invariant_breach("reduce-only fill does not reduce existing position for " + fill.symbol);
        }
        const double reducible = std::abs(current_signed);
        const double requested = std::abs(signed_fill);
        const double effective = std::min(reducible, requested);
        if (effective <= kEpsilon) {
            throw_invariant_breach("reduce-only fill has zero effective quantity for " + fill.symbol);
        }
        signed_fill = signed_fill > 0.0 ? effective : -effective;
    }

    double realized_pnl = 0.0;
    if (position_slot.has_value()) {
        const auto& position = runtime_state.positions[*position_slot];
        const double current_signed = position.is_long ? position.quantity : -position.quantity;
        if (current_signed * signed_fill < 0.0) {
            const double close_quantity = std::min(std::abs(current_signed), std::abs(signed_fill));
            realized_pnl = compute_perp_realized_pnl_for_close(position, close_quantity, fill.price);
        }
    }

    if (!position_slot.has_value() ||
        (runtime_state.merge_positions_enabled == false &&
            ((runtime_state.positions[*position_slot].is_long && signed_fill > 0.0) ||
                (!runtime_state.positions[*position_slot].is_long && signed_fill < 0.0)))) {
        append_perp_position(
            runtime_state,
            step_state,
            fill,
            resolved_symbol_id,
            std::abs(signed_fill),
            signed_fill > 0.0,
            fee,
            fee_rate);
        account.apply_perp_wallet_delta(realized_pnl - fee);
        return true;
    }

    auto& position = runtime_state.positions[*position_slot];
    const double current_signed = position.is_long ? position.quantity : -position.quantity;
    const double next_signed = current_signed + signed_fill;
    if (std::abs(next_signed) <= kEpsilon) {
        erase_position_slot(runtime_state, *position_slot);
        account.apply_perp_wallet_delta(realized_pnl - fee);
        return true;
    }

    if ((current_signed > 0.0 && signed_fill > 0.0) || (current_signed < 0.0 && signed_fill < 0.0)) {
        const double before = std::abs(current_signed);
        const double after = std::abs(next_signed);
        position.entry_price = ((position.entry_price * before) + (fill.price * std::abs(signed_fill))) / after;
    }
    else if ((current_signed > 0.0 && next_signed < 0.0) || (current_signed < 0.0 && next_signed > 0.0)) {
        position.entry_price = fill.price;
    }

    position.quantity = std::abs(next_signed);
    position.is_long = next_signed > 0.0;
    position.fee += fee;
    position.fee_rate = fee_rate;
    position.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    refresh_perp_position_risk_fields(runtime_state, position, step_state, fill_symbol_id);
    reindex_position_slot(runtime_state, *position_slot, resolved_symbol_id);
    account.apply_perp_wallet_delta(realized_pnl - fee);
    return true;
}

void apply_impl(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    const State::StepKernelState* step_state,
    const std::vector<MatchFill>& fills)
{
    normalize_legacy_spot_inventory(runtime_state, step_state);
    if (!ensure_position_index(runtime_state, step_state)) {
        if (!runtime_state.positions.empty()) {
            throw_invariant_breach("unable to rebuild authoritative position index before settlement");
        }
        runtime_state.position_symbol_id_by_slot.clear();
    }

    bool positions_mutated = false;
    for (const auto& fill : fills) {
        if (fill.quantity <= kEpsilon || fill.price <= 0.0) {
            continue;
        }
        if (fill.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
            positions_mutated = apply_spot_fill(runtime_state, account, step_state, fill) || positions_mutated;
        }
        else {
            positions_mutated = apply_perp_fill(runtime_state, account, step_state, fill) || positions_mutated;
        }
        assert_position_index_invariants(runtime_state, step_state);
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
