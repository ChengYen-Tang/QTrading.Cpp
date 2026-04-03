#pragma once

#include <cstdint>
#include <vector>

#include "Exchanges/BinanceSimulator/Contracts/BinanceExchangeRuntimeTypes.hpp"
#include "Exchanges/BinanceSimulator/Contracts/StatusPriceSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// Public read model returned by `FillStatusSnapshot()`.
struct StatusSnapshot {
    /// Exchange timestamp of the snapshot.
    uint64_t ts_exchange{ 0 };
    /// Aggregated wallet balance exposed for legacy-facing consumers.
    double wallet_balance{ 0.0 };
    /// Aggregated margin balance exposed for legacy-facing consumers.
    double margin_balance{ 0.0 };
    /// Aggregated available balance exposed for legacy-facing consumers.
    double available_balance{ 0.0 };
    /// Aggregated unrealized pnl exposed for legacy-facing consumers.
    double unrealized_pnl{ 0.0 };
    /// Sum of unrealized pnl across all open positions.
    double total_unrealized_pnl{ 0.0 };
    /// Perp-ledger wallet balance.
    double perp_wallet_balance{ 0.0 };
    /// Perp-ledger margin balance.
    double perp_margin_balance{ 0.0 };
    /// Perp-ledger available balance.
    double perp_available_balance{ 0.0 };
    /// Spot-ledger cash balance.
    double spot_cash_balance{ 0.0 };
    /// Spot-ledger available cash after reservations.
    double spot_available_balance{ 0.0 };
    /// Marked value of spot inventory currently held.
    double spot_inventory_value{ 0.0 };
    /// Total spot-ledger value including cash and inventory.
    double spot_ledger_value{ 0.0 };
    /// Aggregate cash across spot and perp ledgers.
    double total_cash_balance{ 0.0 };
    /// Aggregate ledger value at the current reference prices.
    double total_ledger_value{ 0.0 };
    /// Conservative lower-bound ledger value.
    double total_ledger_value_base{ 0.0 };
    /// Conservative uncertainty-adjusted ledger value.
    double total_ledger_value_conservative{ 0.0 };
    /// Optimistic uncertainty-adjusted ledger value.
    double total_ledger_value_optimistic{ 0.0 };
    /// Uncertainty band currently applied to conservative/optimistic values.
    double uncertainty_band_bps{ 0.0 };
    /// Number of symbols currently raising a basis warning.
    uint32_t basis_warning_symbols{ 0 };
    /// Number of symbols currently raising a basis stress condition.
    uint32_t basis_stress_symbols{ 0 };
    /// Cumulative count of orders blocked by basis stress.
    uint64_t basis_stress_blocked_orders{ 0 };
    /// Cumulative count of funding applications.
    uint64_t funding_applied_events{ 0 };
    /// Cumulative count of funding rows skipped for missing mark price.
    uint64_t funding_skipped_no_mark{ 0 };
    /// Replay completion percentage in [0, 100].
    double progress_pct{ 0.0 };
    /// Per-symbol trade/mark/index snapshot rows.
    std::vector<StatusPriceSnapshot> prices;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
