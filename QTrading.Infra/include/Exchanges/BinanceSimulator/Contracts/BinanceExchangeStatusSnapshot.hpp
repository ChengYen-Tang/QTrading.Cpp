#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

struct StatusSnapshot {
    struct PriceSnapshot {
        std::string symbol;
        double price{ 0.0 };
        bool has_price{ false };
        double trade_price{ 0.0 };
        bool has_trade_price{ false };
        double mark_price{ 0.0 };
        bool has_mark_price{ false };
        int32_t mark_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
        double index_price{ 0.0 };
        bool has_index_price{ false };
        int32_t index_price_source{ static_cast<int32_t>(ReferencePriceSource::None) };
    };

    uint64_t ts_exchange{ 0 };
    double wallet_balance{ 0.0 };
    double margin_balance{ 0.0 };
    double available_balance{ 0.0 };
    double unrealized_pnl{ 0.0 };
    double total_unrealized_pnl{ 0.0 };
    double perp_wallet_balance{ 0.0 };
    double perp_margin_balance{ 0.0 };
    double perp_available_balance{ 0.0 };
    double spot_cash_balance{ 0.0 };
    double spot_available_balance{ 0.0 };
    double spot_inventory_value{ 0.0 };
    double spot_ledger_value{ 0.0 };
    double total_cash_balance{ 0.0 };
    double total_ledger_value{ 0.0 };
    double total_ledger_value_base{ 0.0 };
    double total_ledger_value_conservative{ 0.0 };
    double total_ledger_value_optimistic{ 0.0 };
    double uncertainty_band_bps{ 0.0 };
    uint32_t basis_warning_symbols{ 0 };
    uint32_t basis_stress_symbols{ 0 };
    uint64_t basis_stress_blocked_orders{ 0 };
    uint64_t funding_applied_events{ 0 };
    uint64_t funding_skipped_no_mark{ 0 };
    double progress_pct{ 0.0 };
    std::vector<PriceSnapshot> prices;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
