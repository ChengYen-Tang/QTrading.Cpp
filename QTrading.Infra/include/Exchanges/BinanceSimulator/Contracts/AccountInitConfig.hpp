#pragma once

namespace QTrading::Infra::Exchanges::BinanceSim::Contracts {

/// Bootstrap parameters for constructing simulator account ledgers and mode flags.
struct AccountInitConfig {
    /// Legacy-style aggregate starting balance retained for diagnostics/bootstrap metadata.
    double init_balance{ 1'000'000.0 };
    /// Initial cash assigned to the spot ledger.
    double spot_initial_cash{ 1'000'000.0 };
    /// Initial wallet balance assigned to the perp ledger.
    double perp_initial_wallet{ 0.0 };
    /// Binance VIP tier used by fee-rate lookup.
    int vip_level{ 0 };
    /// Enables separate long/short position books when true.
    bool hedge_mode{ false };
    /// Enables stricter Binance-style validation and reject behavior.
    bool strict_binance_mode{ true };
    /// Controls whether compatible fills can merge into an existing position record.
    bool merge_positions_enabled{ true };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Contracts
