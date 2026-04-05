#include "Strategy/StrategyConfigLoader.hpp"

#include <rapidjson/document.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace QTrading::Strategy {

namespace {

const rapidjson::Value* FindObject(const rapidjson::Value& root, const char* key)
{
    if (!root.IsObject()) {
        return nullptr;
    }
    const auto member = root.FindMember(key);
    if (member == root.MemberEnd() || !member->value.IsObject()) {
        return nullptr;
    }
    return &member->value;
}

void ApplyString(const rapidjson::Value* obj, const char* key, std::string& out)
{
    if (obj == nullptr) {
        return;
    }
    const auto member = obj->FindMember(key);
    if (member == obj->MemberEnd() || !member->value.IsString()) {
        return;
    }
    out = member->value.GetString();
}

void ApplyBool(const rapidjson::Value* obj, const char* key, bool& out)
{
    if (obj == nullptr) {
        return;
    }
    const auto member = obj->FindMember(key);
    if (member == obj->MemberEnd() || !member->value.IsBool()) {
        return;
    }
    out = member->value.GetBool();
}

template <typename T>
void ApplyNumber(const rapidjson::Value* obj, const char* key, T& out)
{
    if (obj == nullptr) {
        return;
    }
    const auto member = obj->FindMember(key);
    if (member == obj->MemberEnd() || !member->value.IsNumber()) {
        return;
    }
    if constexpr (std::is_same_v<T, double>) {
        out = member->value.GetDouble();
    }
    else if constexpr (std::is_same_v<T, uint64_t>) {
        out = static_cast<uint64_t>(member->value.GetUint64());
    }
    else if constexpr (std::is_same_v<T, uint32_t>) {
        out = static_cast<uint32_t>(member->value.GetUint());
    }
    else if constexpr (std::is_same_v<T, std::size_t>) {
        out = static_cast<std::size_t>(member->value.GetUint64());
    }
}

void ParseStrategyConfigDocument(
    const std::filesystem::path& config_path,
    const char* strategy_name,
    rapidjson::Document& out_doc)
{
    std::ifstream in(config_path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            std::string("Failed to open ") + strategy_name + " config: " + config_path.string());
    }

    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out_doc.Parse(json.c_str());
    if (out_doc.HasParseError() || !out_doc.IsObject()) {
        throw std::runtime_error(
            std::string("Invalid ") + strategy_name + " config json: " + config_path.string());
    }
}

void ApplySharedStrategyConfigSections(
    const rapidjson::Document& doc,
    StrategyModuleConfigs& configs)
{
    const rapidjson::Value* signal = FindObject(doc, "signal");
    ApplyString(signal, "spot_symbol", configs.signal_cfg.spot_symbol);
    ApplyString(signal, "perp_symbol", configs.signal_cfg.perp_symbol);
    ApplyNumber(signal, "entry_min_funding_rate", configs.signal_cfg.entry_min_funding_rate);
    ApplyNumber(signal, "exit_min_funding_rate", configs.signal_cfg.exit_min_funding_rate);
    ApplyNumber(signal, "hard_negative_funding_rate", configs.signal_cfg.hard_negative_funding_rate);
    ApplyNumber(signal, "entry_max_basis_pct", configs.signal_cfg.entry_max_basis_pct);
    ApplyNumber(signal, "exit_max_basis_pct", configs.signal_cfg.exit_max_basis_pct);
    ApplyNumber(signal, "cooldown_ms", configs.signal_cfg.cooldown_ms);
    ApplyNumber(signal, "min_hold_ms", configs.signal_cfg.min_hold_ms);
    ApplyNumber(signal, "entry_persistence_settlements", configs.signal_cfg.entry_persistence_settlements);
    ApplyNumber(signal, "exit_persistence_settlements", configs.signal_cfg.exit_persistence_settlements);
    ApplyBool(signal, "adaptive_confidence_enabled", configs.signal_cfg.adaptive_confidence_enabled);
    ApplyBool(signal, "adaptive_structure_enabled", configs.signal_cfg.adaptive_structure_enabled);
    ApplyBool(signal, "funding_nowcast_enabled", configs.signal_cfg.funding_nowcast_enabled);
    ApplyNumber(signal, "funding_nowcast_interval_ms", configs.signal_cfg.funding_nowcast_interval_ms);
    ApplyBool(signal, "funding_nowcast_use_for_gates", configs.signal_cfg.funding_nowcast_use_for_gates);
    ApplyBool(signal, "funding_nowcast_use_for_entry_gate", configs.signal_cfg.funding_nowcast_use_for_entry_gate);
    ApplyBool(signal, "funding_nowcast_use_for_exit_gate", configs.signal_cfg.funding_nowcast_use_for_exit_gate);
    ApplyBool(signal, "funding_nowcast_use_for_confidence", configs.signal_cfg.funding_nowcast_use_for_confidence);
    ApplyNumber(signal, "funding_nowcast_gate_sample_ms", configs.signal_cfg.funding_nowcast_gate_sample_ms);
    ApplyBool(signal, "pre_settlement_negative_exit_enabled", configs.signal_cfg.pre_settlement_negative_exit_enabled);
    ApplyNumber(signal, "pre_settlement_negative_exit_threshold", configs.signal_cfg.pre_settlement_negative_exit_threshold);
    ApplyNumber(signal, "pre_settlement_negative_exit_lookahead_ms", configs.signal_cfg.pre_settlement_negative_exit_lookahead_ms);
    ApplyNumber(signal, "pre_settlement_negative_exit_reentry_buffer_ms", configs.signal_cfg.pre_settlement_negative_exit_reentry_buffer_ms);
    ApplyBool(signal, "pre_settlement_negative_exit_require_funding_gate", configs.signal_cfg.pre_settlement_negative_exit_require_funding_gate);

    const rapidjson::Value* intent = FindObject(doc, "intent");
    ApplyString(intent, "spot_symbol", configs.intent_cfg.spot_symbol);
    ApplyString(intent, "perp_symbol", configs.intent_cfg.perp_symbol);
    ApplyBool(intent, "receive_funding", configs.intent_cfg.receive_funding);

    const rapidjson::Value* risk = FindObject(doc, "risk");
    ApplyNumber(risk, "notional_usdt", configs.risk_cfg.notional_usdt);
    ApplyNumber(risk, "leverage", configs.risk_cfg.leverage);
    ApplyNumber(risk, "max_leverage", configs.risk_cfg.max_leverage);
    ApplyNumber(risk, "rebalance_threshold_ratio", configs.risk_cfg.rebalance_threshold_ratio);
    ApplyNumber(risk, "dual_ledger_auto_notional_ratio", configs.risk_cfg.dual_ledger_auto_notional_ratio);
    ApplyBool(risk, "carry_allocator_leverage_model_enabled", configs.risk_cfg.carry_allocator_leverage_model_enabled);
    ApplyNumber(risk, "carry_allocator_spot_cash_per_notional", configs.risk_cfg.carry_allocator_spot_cash_per_notional);
    ApplyNumber(risk, "carry_allocator_perp_margin_buffer_ratio", configs.risk_cfg.carry_allocator_perp_margin_buffer_ratio);
    ApplyNumber(risk, "carry_allocator_perp_leverage", configs.risk_cfg.carry_allocator_perp_leverage);
    ApplyNumber(risk, "perp_liq_buffer_floor_ratio", configs.risk_cfg.perp_liq_buffer_floor_ratio);
    ApplyNumber(risk, "perp_liq_buffer_ceiling_ratio", configs.risk_cfg.perp_liq_buffer_ceiling_ratio);
    ApplyNumber(risk, "perp_liq_min_notional_scale", configs.risk_cfg.perp_liq_min_notional_scale);
    ApplyBool(risk, "carry_core_overlay_enabled", configs.risk_cfg.carry_core_overlay_enabled);
    ApplyNumber(risk, "carry_core_notional_ratio", configs.risk_cfg.carry_core_notional_ratio);
    ApplyNumber(risk, "carry_overlay_notional_ratio", configs.risk_cfg.carry_overlay_notional_ratio);
    ApplyNumber(risk, "carry_overlay_confidence_power", configs.risk_cfg.carry_overlay_confidence_power);
    ApplyBool(risk, "carry_confidence_boost_enabled", configs.risk_cfg.carry_confidence_boost_enabled);
    ApplyNumber(risk, "carry_confidence_boost_reference", configs.risk_cfg.carry_confidence_boost_reference);
    ApplyNumber(risk, "carry_confidence_boost_max_scale", configs.risk_cfg.carry_confidence_boost_max_scale);
    ApplyNumber(risk, "carry_confidence_boost_power", configs.risk_cfg.carry_confidence_boost_power);
    ApplyNumber(risk, "carry_size_cost_rate_per_leg", configs.risk_cfg.carry_size_cost_rate_per_leg);
    ApplyNumber(risk, "carry_size_expected_hold_settlements", configs.risk_cfg.carry_size_expected_hold_settlements);
    ApplyNumber(risk, "carry_size_min_gain_to_cost", configs.risk_cfg.carry_size_min_gain_to_cost);
    ApplyNumber(risk, "carry_size_min_gain_to_cost_low_confidence", configs.risk_cfg.carry_size_min_gain_to_cost_low_confidence);
    ApplyNumber(risk, "carry_size_min_gain_to_cost_high_confidence", configs.risk_cfg.carry_size_min_gain_to_cost_high_confidence);
    ApplyNumber(risk, "carry_size_gain_to_cost_confidence_power", configs.risk_cfg.carry_size_gain_to_cost_confidence_power);
    ApplyBool(risk, "basis_alpha_overlay_enabled", configs.risk_cfg.basis_alpha_overlay_enabled);
    ApplyNumber(risk, "basis_alpha_overlay_center_pct", configs.risk_cfg.basis_alpha_overlay_center_pct);
    ApplyNumber(risk, "basis_alpha_overlay_band_pct", configs.risk_cfg.basis_alpha_overlay_band_pct);
    ApplyNumber(risk, "basis_alpha_overlay_upscale_cap", configs.risk_cfg.basis_alpha_overlay_upscale_cap);
    ApplyNumber(risk, "basis_alpha_overlay_downscale_floor", configs.risk_cfg.basis_alpha_overlay_downscale_floor);

    const rapidjson::Value* execution = FindObject(doc, "execution");
    ApplyNumber(execution, "min_notional", configs.execution_cfg.min_notional);
    ApplyNumber(execution, "carry_rebalance_cooldown_ms", configs.execution_cfg.carry_rebalance_cooldown_ms);
    ApplyNumber(execution, "carry_max_rebalance_step_ratio", configs.execution_cfg.carry_max_rebalance_step_ratio);
    ApplyNumber(execution, "carry_max_participation_rate", configs.execution_cfg.carry_max_participation_rate);
    ApplyBool(execution, "carry_maker_first_enabled", configs.execution_cfg.carry_maker_first_enabled);
    ApplyNumber(execution, "carry_maker_limit_offset_bps", configs.execution_cfg.carry_maker_limit_offset_bps);
    ApplyNumber(execution, "carry_maker_catchup_gap_ratio", configs.execution_cfg.carry_maker_catchup_gap_ratio);
    ApplyBool(execution, "carry_target_anchor_enabled", configs.execution_cfg.carry_target_anchor_enabled);
    ApplyNumber(execution, "carry_target_anchor_update_ratio", configs.execution_cfg.carry_target_anchor_update_ratio);
    ApplyBool(execution, "carry_confidence_adaptive_enabled", configs.execution_cfg.carry_confidence_adaptive_enabled);

    const rapidjson::Value* monitoring = FindObject(doc, "monitoring");
    ApplyNumber(monitoring, "max_open_orders_per_symbol", configs.monitoring_cfg.max_open_orders_per_symbol);
}

void ApplyBasisArbitrageSpecificConfigSections(
    const rapidjson::Document& doc,
    StrategyModuleConfigs& configs)
{
    const rapidjson::Value* signal = FindObject(doc, "signal");
    ApplyNumber(signal, "mark_index_soft_derisk_start_bps", configs.signal_cfg.mark_index_soft_derisk_start_bps);
    ApplyNumber(signal, "mark_index_soft_derisk_full_bps", configs.signal_cfg.mark_index_soft_derisk_full_bps);
    ApplyNumber(signal, "mark_index_soft_derisk_min_confidence_scale", configs.signal_cfg.mark_index_soft_derisk_min_confidence_scale);
    ApplyNumber(signal, "mark_index_hard_exit_bps", configs.signal_cfg.mark_index_hard_exit_bps);
    ApplyBool(signal, "basis_mr_enabled", configs.signal_cfg.basis_mr_enabled);
    ApplyBool(signal, "basis_mr_use_mark_index", configs.signal_cfg.basis_mr_use_mark_index);
    ApplyNumber(signal, "basis_mr_window_bars", configs.signal_cfg.basis_mr_window_bars);
    ApplyNumber(signal, "basis_mr_min_samples", configs.signal_cfg.basis_mr_min_samples);
    ApplyNumber(signal, "basis_mr_entry_z", configs.signal_cfg.basis_mr_entry_z);
    ApplyNumber(signal, "basis_mr_exit_z", configs.signal_cfg.basis_mr_exit_z);
    ApplyNumber(signal, "basis_mr_max_abs_z", configs.signal_cfg.basis_mr_max_abs_z);
    ApplyNumber(signal, "basis_mr_entry_persistence_bars", configs.signal_cfg.basis_mr_entry_persistence_bars);
    ApplyNumber(signal, "basis_mr_exit_persistence_bars", configs.signal_cfg.basis_mr_exit_persistence_bars);
    ApplyNumber(signal, "basis_mr_cooldown_ms", configs.signal_cfg.basis_mr_cooldown_ms);
    ApplyNumber(signal, "basis_mr_std_floor", configs.signal_cfg.basis_mr_std_floor);
    ApplyBool(signal, "basis_mr_confidence_from_z", configs.signal_cfg.basis_mr_confidence_from_z);
    ApplyNumber(signal, "basis_mr_confidence_floor", configs.signal_cfg.basis_mr_confidence_floor);
    ApplyBool(signal, "basis_regime_confidence_enabled", configs.signal_cfg.basis_regime_confidence_enabled);
    ApplyBool(signal, "basis_regime_use_mark_index", configs.signal_cfg.basis_regime_use_mark_index);
    ApplyNumber(signal, "basis_regime_window_bars", configs.signal_cfg.basis_regime_window_bars);
    ApplyNumber(signal, "basis_regime_min_samples", configs.signal_cfg.basis_regime_min_samples);
    ApplyNumber(signal, "basis_regime_calm_z", configs.signal_cfg.basis_regime_calm_z);
    ApplyNumber(signal, "basis_regime_stress_z", configs.signal_cfg.basis_regime_stress_z);
    ApplyNumber(signal, "basis_regime_min_confidence_scale", configs.signal_cfg.basis_regime_min_confidence_scale);
    ApplyNumber(signal, "basis_stop_alpha_z", configs.signal_cfg.basis_stop_alpha_z);
    ApplyNumber(signal, "basis_stop_risk_z", configs.signal_cfg.basis_stop_risk_z);
    ApplyBool(signal, "basis_cost_gate_enabled", configs.signal_cfg.basis_cost_gate_enabled);
    ApplyNumber(signal, "basis_cost_edge_threshold_pct", configs.signal_cfg.basis_cost_edge_threshold_pct);
    ApplyNumber(signal, "basis_cost_expected_hold_hours", configs.signal_cfg.basis_cost_expected_hold_hours);
    ApplyNumber(signal, "basis_cost_expected_funding_settlements", configs.signal_cfg.basis_cost_expected_funding_settlements);
    ApplyNumber(signal, "basis_cost_borrow_apr", configs.signal_cfg.basis_cost_borrow_apr);
    ApplyNumber(signal, "basis_cost_trading_cost_rate_per_leg", configs.signal_cfg.basis_cost_trading_cost_rate_per_leg);
    ApplyNumber(signal, "basis_cost_risk_penalty_weight", configs.signal_cfg.basis_cost_risk_penalty_weight);
    ApplyNumber(signal, "basis_cost_trend_penalty_weight", configs.signal_cfg.basis_cost_trend_penalty_weight);
    ApplyBool(signal, "basis_cost_include_funding", configs.signal_cfg.basis_cost_include_funding);

    const rapidjson::Value* intent = FindObject(doc, "intent");
    ApplyBool(intent, "basis_directional_enabled", configs.intent_cfg.basis_directional_enabled);
    ApplyBool(intent, "basis_direction_use_mark_index", configs.intent_cfg.basis_direction_use_mark_index);
    ApplyNumber(intent, "basis_direction_switch_entry_abs_pct", configs.intent_cfg.basis_direction_switch_entry_abs_pct);
    ApplyNumber(intent, "basis_direction_switch_exit_abs_pct", configs.intent_cfg.basis_direction_switch_exit_abs_pct);
    ApplyNumber(intent, "basis_direction_switch_cooldown_ms", configs.intent_cfg.basis_direction_switch_cooldown_ms);

    const rapidjson::Value* risk = FindObject(doc, "risk");
    ApplyNumber(risk, "mark_index_soft_derisk_start_bps", configs.risk_cfg.mark_index_soft_derisk_start_bps);
    ApplyNumber(risk, "mark_index_soft_derisk_full_bps", configs.risk_cfg.mark_index_soft_derisk_full_bps);
    ApplyNumber(risk, "mark_index_soft_derisk_min_scale", configs.risk_cfg.mark_index_soft_derisk_min_scale);
    ApplyNumber(risk, "mark_index_hard_guard_bps", configs.risk_cfg.mark_index_hard_guard_bps);
}

} // namespace

void LoadFundingCarryConfig(
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs)
{
    rapidjson::Document doc;
    ParseStrategyConfigDocument(config_path, "funding-carry", doc);
    ApplySharedStrategyConfigSections(doc, configs);
}

void LoadBasisArbitrageConfig(
    const std::filesystem::path& config_path,
    StrategyModuleConfigs& configs)
{
    rapidjson::Document doc;
    ParseStrategyConfigDocument(config_path, "basis-arbitrage", doc);
    ApplySharedStrategyConfigSections(doc, configs);
    ApplyBasisArbitrageSpecificConfigSections(doc, configs);
}

} // namespace QTrading::Strategy
