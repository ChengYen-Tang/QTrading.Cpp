#include "ServiceHelpers.hpp"

#include <rapidjson/document.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sstream>
#include <type_traits>

namespace QTrading::Service::Helpers {

namespace {

std::atomic<bool> g_stop_requested{ false };

void HandleSignal(int)
{
    g_stop_requested.store(true, std::memory_order_relaxed);
}

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
    QTrading::Signal::FundingCarrySignalEngine::Config& signal_cfg,
    QTrading::Intent::FundingCarryIntentBuilder::Config& intent_cfg,
    QTrading::Risk::SimpleRiskEngine::Config& risk_cfg,
    QTrading::Execution::MarketExecutionEngine::Config& execution_cfg,
    QTrading::Monitoring::SimpleMonitoring::Config& monitoring_cfg)
{
    const rapidjson::Value* signal = FindObject(doc, "signal");
    ApplyString(signal, "spot_symbol", signal_cfg.spot_symbol);
    ApplyString(signal, "perp_symbol", signal_cfg.perp_symbol);
    ApplyNumber(signal, "entry_min_funding_rate", signal_cfg.entry_min_funding_rate);
    ApplyNumber(signal, "exit_min_funding_rate", signal_cfg.exit_min_funding_rate);
    ApplyNumber(signal, "hard_negative_funding_rate", signal_cfg.hard_negative_funding_rate);
    ApplyNumber(signal, "entry_max_basis_pct", signal_cfg.entry_max_basis_pct);
    ApplyNumber(signal, "exit_max_basis_pct", signal_cfg.exit_max_basis_pct);
    ApplyNumber(signal, "cooldown_ms", signal_cfg.cooldown_ms);
    ApplyNumber(signal, "min_hold_ms", signal_cfg.min_hold_ms);
    ApplyNumber(signal, "entry_persistence_settlements", signal_cfg.entry_persistence_settlements);
    ApplyNumber(signal, "exit_persistence_settlements", signal_cfg.exit_persistence_settlements);
    ApplyBool(signal, "adaptive_confidence_enabled", signal_cfg.adaptive_confidence_enabled);
    ApplyBool(signal, "adaptive_structure_enabled", signal_cfg.adaptive_structure_enabled);
    ApplyBool(signal, "funding_nowcast_enabled", signal_cfg.funding_nowcast_enabled);
    ApplyNumber(signal, "funding_nowcast_interval_ms", signal_cfg.funding_nowcast_interval_ms);
    ApplyBool(signal, "funding_nowcast_use_for_gates", signal_cfg.funding_nowcast_use_for_gates);
    ApplyBool(signal, "funding_nowcast_use_for_entry_gate", signal_cfg.funding_nowcast_use_for_entry_gate);
    ApplyBool(signal, "funding_nowcast_use_for_exit_gate", signal_cfg.funding_nowcast_use_for_exit_gate);
    ApplyBool(signal, "funding_nowcast_use_for_confidence", signal_cfg.funding_nowcast_use_for_confidence);
    ApplyNumber(signal, "funding_nowcast_gate_sample_ms", signal_cfg.funding_nowcast_gate_sample_ms);
    ApplyBool(signal, "pre_settlement_negative_exit_enabled", signal_cfg.pre_settlement_negative_exit_enabled);
    ApplyNumber(signal, "pre_settlement_negative_exit_threshold", signal_cfg.pre_settlement_negative_exit_threshold);
    ApplyNumber(signal, "pre_settlement_negative_exit_lookahead_ms", signal_cfg.pre_settlement_negative_exit_lookahead_ms);
    ApplyNumber(signal, "pre_settlement_negative_exit_reentry_buffer_ms", signal_cfg.pre_settlement_negative_exit_reentry_buffer_ms);
    ApplyBool(signal, "pre_settlement_negative_exit_require_funding_gate", signal_cfg.pre_settlement_negative_exit_require_funding_gate);

    const rapidjson::Value* intent = FindObject(doc, "intent");
    ApplyString(intent, "spot_symbol", intent_cfg.spot_symbol);
    ApplyString(intent, "perp_symbol", intent_cfg.perp_symbol);
    ApplyBool(intent, "receive_funding", intent_cfg.receive_funding);

    const rapidjson::Value* risk = FindObject(doc, "risk");
    ApplyNumber(risk, "notional_usdt", risk_cfg.notional_usdt);
    ApplyNumber(risk, "leverage", risk_cfg.leverage);
    ApplyNumber(risk, "max_leverage", risk_cfg.max_leverage);
    ApplyNumber(risk, "rebalance_threshold_ratio", risk_cfg.rebalance_threshold_ratio);
    ApplyNumber(risk, "dual_ledger_auto_notional_ratio", risk_cfg.dual_ledger_auto_notional_ratio);
    ApplyBool(risk, "carry_allocator_leverage_model_enabled", risk_cfg.carry_allocator_leverage_model_enabled);
    ApplyNumber(risk, "carry_allocator_spot_cash_per_notional", risk_cfg.carry_allocator_spot_cash_per_notional);
    ApplyNumber(risk, "carry_allocator_perp_margin_buffer_ratio", risk_cfg.carry_allocator_perp_margin_buffer_ratio);
    ApplyNumber(risk, "carry_allocator_perp_leverage", risk_cfg.carry_allocator_perp_leverage);
    ApplyNumber(risk, "perp_liq_buffer_floor_ratio", risk_cfg.perp_liq_buffer_floor_ratio);
    ApplyNumber(risk, "perp_liq_buffer_ceiling_ratio", risk_cfg.perp_liq_buffer_ceiling_ratio);
    ApplyNumber(risk, "perp_liq_min_notional_scale", risk_cfg.perp_liq_min_notional_scale);
    ApplyBool(risk, "carry_core_overlay_enabled", risk_cfg.carry_core_overlay_enabled);
    ApplyNumber(risk, "carry_core_notional_ratio", risk_cfg.carry_core_notional_ratio);
    ApplyNumber(risk, "carry_overlay_notional_ratio", risk_cfg.carry_overlay_notional_ratio);
    ApplyNumber(risk, "carry_overlay_confidence_power", risk_cfg.carry_overlay_confidence_power);
    ApplyBool(risk, "carry_confidence_boost_enabled", risk_cfg.carry_confidence_boost_enabled);
    ApplyNumber(risk, "carry_confidence_boost_reference", risk_cfg.carry_confidence_boost_reference);
    ApplyNumber(risk, "carry_confidence_boost_max_scale", risk_cfg.carry_confidence_boost_max_scale);
    ApplyNumber(risk, "carry_confidence_boost_power", risk_cfg.carry_confidence_boost_power);
    ApplyNumber(risk, "carry_size_cost_rate_per_leg", risk_cfg.carry_size_cost_rate_per_leg);
    ApplyNumber(risk, "carry_size_expected_hold_settlements", risk_cfg.carry_size_expected_hold_settlements);
    ApplyNumber(risk, "carry_size_min_gain_to_cost", risk_cfg.carry_size_min_gain_to_cost);
    ApplyNumber(risk, "carry_size_min_gain_to_cost_low_confidence", risk_cfg.carry_size_min_gain_to_cost_low_confidence);
    ApplyNumber(risk, "carry_size_min_gain_to_cost_high_confidence", risk_cfg.carry_size_min_gain_to_cost_high_confidence);
    ApplyNumber(risk, "carry_size_gain_to_cost_confidence_power", risk_cfg.carry_size_gain_to_cost_confidence_power);
    ApplyBool(risk, "basis_alpha_overlay_enabled", risk_cfg.basis_alpha_overlay_enabled);
    ApplyNumber(risk, "basis_alpha_overlay_center_pct", risk_cfg.basis_alpha_overlay_center_pct);
    ApplyNumber(risk, "basis_alpha_overlay_band_pct", risk_cfg.basis_alpha_overlay_band_pct);
    ApplyNumber(risk, "basis_alpha_overlay_upscale_cap", risk_cfg.basis_alpha_overlay_upscale_cap);
    ApplyNumber(risk, "basis_alpha_overlay_downscale_floor", risk_cfg.basis_alpha_overlay_downscale_floor);

    const rapidjson::Value* execution = FindObject(doc, "execution");
    ApplyNumber(execution, "min_notional", execution_cfg.min_notional);
    ApplyNumber(execution, "carry_rebalance_cooldown_ms", execution_cfg.carry_rebalance_cooldown_ms);
    ApplyNumber(execution, "carry_max_rebalance_step_ratio", execution_cfg.carry_max_rebalance_step_ratio);
    ApplyNumber(execution, "carry_max_participation_rate", execution_cfg.carry_max_participation_rate);
    ApplyBool(execution, "carry_maker_first_enabled", execution_cfg.carry_maker_first_enabled);
    ApplyNumber(execution, "carry_maker_limit_offset_bps", execution_cfg.carry_maker_limit_offset_bps);
    ApplyNumber(execution, "carry_maker_catchup_gap_ratio", execution_cfg.carry_maker_catchup_gap_ratio);
    ApplyBool(execution, "carry_target_anchor_enabled", execution_cfg.carry_target_anchor_enabled);
    ApplyNumber(execution, "carry_target_anchor_update_ratio", execution_cfg.carry_target_anchor_update_ratio);
    ApplyBool(execution, "carry_confidence_adaptive_enabled", execution_cfg.carry_confidence_adaptive_enabled);

    const rapidjson::Value* monitoring = FindObject(doc, "monitoring");
    ApplyNumber(monitoring, "max_open_orders_per_symbol", monitoring_cfg.max_open_orders_per_symbol);
}

void ApplyBasisArbitrageSpecificConfigSections(
    const rapidjson::Document& doc,
    QTrading::Signal::FundingCarrySignalEngine::Config& signal_cfg,
    QTrading::Intent::FundingCarryIntentBuilder::Config& intent_cfg,
    QTrading::Risk::SimpleRiskEngine::Config& risk_cfg)
{
    const rapidjson::Value* signal = FindObject(doc, "signal");
    ApplyNumber(signal, "mark_index_soft_derisk_start_bps", signal_cfg.mark_index_soft_derisk_start_bps);
    ApplyNumber(signal, "mark_index_soft_derisk_full_bps", signal_cfg.mark_index_soft_derisk_full_bps);
    ApplyNumber(signal, "mark_index_soft_derisk_min_confidence_scale", signal_cfg.mark_index_soft_derisk_min_confidence_scale);
    ApplyNumber(signal, "mark_index_hard_exit_bps", signal_cfg.mark_index_hard_exit_bps);
    ApplyBool(signal, "basis_mr_enabled", signal_cfg.basis_mr_enabled);
    ApplyBool(signal, "basis_mr_use_mark_index", signal_cfg.basis_mr_use_mark_index);
    ApplyNumber(signal, "basis_mr_window_bars", signal_cfg.basis_mr_window_bars);
    ApplyNumber(signal, "basis_mr_min_samples", signal_cfg.basis_mr_min_samples);
    ApplyNumber(signal, "basis_mr_entry_z", signal_cfg.basis_mr_entry_z);
    ApplyNumber(signal, "basis_mr_exit_z", signal_cfg.basis_mr_exit_z);
    ApplyNumber(signal, "basis_mr_max_abs_z", signal_cfg.basis_mr_max_abs_z);
    ApplyNumber(signal, "basis_mr_entry_persistence_bars", signal_cfg.basis_mr_entry_persistence_bars);
    ApplyNumber(signal, "basis_mr_exit_persistence_bars", signal_cfg.basis_mr_exit_persistence_bars);
    ApplyNumber(signal, "basis_mr_cooldown_ms", signal_cfg.basis_mr_cooldown_ms);
    ApplyNumber(signal, "basis_mr_std_floor", signal_cfg.basis_mr_std_floor);
    ApplyBool(signal, "basis_mr_confidence_from_z", signal_cfg.basis_mr_confidence_from_z);
    ApplyNumber(signal, "basis_mr_confidence_floor", signal_cfg.basis_mr_confidence_floor);
    ApplyBool(signal, "basis_regime_confidence_enabled", signal_cfg.basis_regime_confidence_enabled);
    ApplyBool(signal, "basis_regime_use_mark_index", signal_cfg.basis_regime_use_mark_index);
    ApplyNumber(signal, "basis_regime_window_bars", signal_cfg.basis_regime_window_bars);
    ApplyNumber(signal, "basis_regime_min_samples", signal_cfg.basis_regime_min_samples);
    ApplyNumber(signal, "basis_regime_calm_z", signal_cfg.basis_regime_calm_z);
    ApplyNumber(signal, "basis_regime_stress_z", signal_cfg.basis_regime_stress_z);
    ApplyNumber(signal, "basis_regime_min_confidence_scale", signal_cfg.basis_regime_min_confidence_scale);
    ApplyNumber(signal, "basis_stop_alpha_z", signal_cfg.basis_stop_alpha_z);
    ApplyNumber(signal, "basis_stop_risk_z", signal_cfg.basis_stop_risk_z);
    ApplyBool(signal, "basis_cost_gate_enabled", signal_cfg.basis_cost_gate_enabled);
    ApplyNumber(signal, "basis_cost_edge_threshold_pct", signal_cfg.basis_cost_edge_threshold_pct);
    ApplyNumber(signal, "basis_cost_expected_hold_hours", signal_cfg.basis_cost_expected_hold_hours);
    ApplyNumber(signal, "basis_cost_expected_funding_settlements", signal_cfg.basis_cost_expected_funding_settlements);
    ApplyNumber(signal, "basis_cost_borrow_apr", signal_cfg.basis_cost_borrow_apr);
    ApplyNumber(signal, "basis_cost_trading_cost_rate_per_leg", signal_cfg.basis_cost_trading_cost_rate_per_leg);
    ApplyNumber(signal, "basis_cost_risk_penalty_weight", signal_cfg.basis_cost_risk_penalty_weight);
    ApplyNumber(signal, "basis_cost_trend_penalty_weight", signal_cfg.basis_cost_trend_penalty_weight);
    ApplyBool(signal, "basis_cost_include_funding", signal_cfg.basis_cost_include_funding);

    const rapidjson::Value* intent = FindObject(doc, "intent");
    ApplyBool(intent, "basis_directional_enabled", intent_cfg.basis_directional_enabled);
    ApplyBool(intent, "basis_direction_use_mark_index", intent_cfg.basis_direction_use_mark_index);
    ApplyNumber(intent, "basis_direction_switch_entry_abs_pct", intent_cfg.basis_direction_switch_entry_abs_pct);
    ApplyNumber(intent, "basis_direction_switch_exit_abs_pct", intent_cfg.basis_direction_switch_exit_abs_pct);
    ApplyNumber(intent, "basis_direction_switch_cooldown_ms", intent_cfg.basis_direction_switch_cooldown_ms);

    const rapidjson::Value* risk = FindObject(doc, "risk");
    ApplyNumber(risk, "mark_index_soft_derisk_start_bps", risk_cfg.mark_index_soft_derisk_start_bps);
    ApplyNumber(risk, "mark_index_soft_derisk_full_bps", risk_cfg.mark_index_soft_derisk_full_bps);
    ApplyNumber(risk, "mark_index_soft_derisk_min_scale", risk_cfg.mark_index_soft_derisk_min_scale);
    ApplyNumber(risk, "mark_index_hard_guard_bps", risk_cfg.mark_index_hard_guard_bps);
}

} // namespace

std::string JsonEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        if (c == '\\') {
            out += "\\\\";
        }
        else if (c == '"') {
            out += "\\\"";
        }
        else {
            out.push_back(c);
        }
    }
    return out;
}

std::string Utf8Path(const char8_t* path)
{
    return std::string(reinterpret_cast<const char*>(path));
}

void SetEnvVar(const char* key, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

void InstallSignalHandlers()
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

bool StopRequested()
{
    return g_stop_requested.load(std::memory_order_relaxed);
}

std::filesystem::path ResolveRepoRelativePath(
    const std::filesystem::path& source_file_path,
    const std::filesystem::path& repo_relative_path)
{
    return source_file_path.parent_path().parent_path() / repo_relative_path;
}

std::string InstrumentTypeToString(std::optional<QTrading::Dto::Trading::InstrumentType> type)
{
    if (!type.has_value()) {
        return "auto";
    }
    switch (*type) {
    case QTrading::Dto::Trading::InstrumentType::Spot:
        return "spot";
    case QTrading::Dto::Trading::InstrumentType::Perp:
        return "perp";
    default:
        break;
    }
    return "unknown";
}

QTrading::Log::LoggerBootstrapConfig BuildLoggerBootstrapConfig(
    const std::filesystem::path& logs_root,
    const std::vector<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::SymbolDataset>& symbol_csv,
    const std::string& strategy_name,
    const std::string& strategy_version,
    const std::string& strategy_params)
{
    std::vector<QTrading::Log::DatasetEntry> dataset_entries;
    dataset_entries.reserve(symbol_csv.size());
    for (const auto& ds : symbol_csv) {
        dataset_entries.push_back(QTrading::Log::DatasetEntry{
            .symbol = ds.symbol,
            .kline_csv = ds.kline_csv,
            .instrument_type = InstrumentTypeToString(ds.instrument_type),
            .funding_csv = ds.funding_csv
        });
    }

    return QTrading::Log::LoggerBootstrapConfig{
        .logs_root = logs_root,
        .strategy_name = strategy_name,
        .strategy_version = strategy_version,
        .strategy_params = strategy_params,
        .dataset_entries = std::move(dataset_entries)
    };
}

void LoadFundingCarryConfig(
    const std::filesystem::path& config_path,
    QTrading::Signal::FundingCarrySignalEngine::Config& signal_cfg,
    QTrading::Intent::FundingCarryIntentBuilder::Config& intent_cfg,
    QTrading::Risk::SimpleRiskEngine::Config& risk_cfg,
    QTrading::Execution::MarketExecutionEngine::Config& execution_cfg,
    QTrading::Monitoring::SimpleMonitoring::Config& monitoring_cfg)
{
    rapidjson::Document doc;
    ParseStrategyConfigDocument(config_path, "funding-carry", doc);
    ApplySharedStrategyConfigSections(
        doc,
        signal_cfg,
        intent_cfg,
        risk_cfg,
        execution_cfg,
        monitoring_cfg);
}

void LoadBasisArbitrageConfig(
    const std::filesystem::path& config_path,
    QTrading::Signal::FundingCarrySignalEngine::Config& signal_cfg,
    QTrading::Intent::FundingCarryIntentBuilder::Config& intent_cfg,
    QTrading::Risk::SimpleRiskEngine::Config& risk_cfg,
    QTrading::Execution::MarketExecutionEngine::Config& execution_cfg,
    QTrading::Monitoring::SimpleMonitoring::Config& monitoring_cfg)
{
    rapidjson::Document doc;
    ParseStrategyConfigDocument(config_path, "basis-arbitrage", doc);
    ApplySharedStrategyConfigSections(
        doc,
        signal_cfg,
        intent_cfg,
        risk_cfg,
        execution_cfg,
        monitoring_cfg);
    ApplyBasisArbitrageSpecificConfigSections(doc, signal_cfg, intent_cfg, risk_cfg);
}

void EmitExchangeStatusLine(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange)
{
    if (!exchange) {
        return;
    }

    QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::StatusSnapshot snap{};
    exchange->FillStatusSnapshot(snap);

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(2);
    oss << "[Service][Status] ts=" << snap.ts_exchange
        << " wallet=" << snap.wallet_balance
        << " margin=" << snap.margin_balance
        << " avail=" << snap.available_balance
        << " u_pnl=" << snap.total_unrealized_pnl
        << " spot_cash=" << snap.spot_cash_balance
        << " spot_inv=" << snap.spot_inventory_value
        << " spot_ledger=" << snap.spot_ledger_value
        << " perp_ledger=" << snap.perp_margin_balance
        << " total_ledger=" << snap.total_ledger_value
        << " progress=" << snap.progress_pct << "%";
    oss << " prices=";
    double max_abs_mark_index_bps = 0.0;
    size_t mark_index_warning_symbols = 0;
    size_t mark_index_stress_symbols = 0;
    constexpr double kMarkIndexWarningBps = 50.0;
    constexpr double kMarkIndexStressBps = 150.0;
    for (size_t i = 0; i < snap.prices.size(); ++i) {
        const auto& p = snap.prices[i];
        if (i > 0) {
            oss << ",";
        }
        oss << p.symbol << "(t=";
        if (p.has_price) {
            oss << p.price;
        }
        else {
            oss << "n/a";
        }
        oss << ",m=";
        if (p.has_mark_price) {
            oss << p.mark_price;
        }
        else {
            oss << "n/a";
        }
        oss << ",i=";
        if (p.has_index_price) {
            oss << p.index_price;
        }
        else {
            oss << "n/a";
        }
        oss << ")";

        if (p.has_mark_price && p.has_index_price && std::abs(p.index_price) > 1e-12) {
            const double basis_bps = ((p.mark_price - p.index_price) / p.index_price) * 10000.0;
            const double abs_basis_bps = std::abs(basis_bps);
            max_abs_mark_index_bps = std::max(max_abs_mark_index_bps, abs_basis_bps);
            if (abs_basis_bps >= kMarkIndexStressBps) {
                ++mark_index_stress_symbols;
            }
            else if (abs_basis_bps >= kMarkIndexWarningBps) {
                ++mark_index_warning_symbols;
            }
        }
    }
    oss << " mi_max_bps=" << max_abs_mark_index_bps;
    if (mark_index_stress_symbols > 0) {
        oss << " mi_alert=stress(" << mark_index_stress_symbols << ")";
    }
    else if (mark_index_warning_symbols > 0) {
        oss << " mi_alert=warning(" << mark_index_warning_symbols << ")";
    }
    std::cout << oss.str() << std::endl;
}

} // namespace QTrading::Service::Helpers
