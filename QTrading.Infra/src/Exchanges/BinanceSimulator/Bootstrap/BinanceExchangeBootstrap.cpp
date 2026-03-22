#include "Exchanges/BinanceSimulator/Bootstrap/BinanceExchangeBootstrap.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap {

std::vector<Contracts::SymbolDataset> ToDatasets(
    const std::vector<std::pair<std::string, std::string>>& symbol_csv)
{
    // Compatibility helper for constructors still using pair-based input.
    std::vector<Contracts::SymbolDataset> datasets;
    datasets.reserve(symbol_csv.size());
    for (const auto& [symbol, csv] : symbol_csv) {
        datasets.push_back(Contracts::SymbolDataset{
            symbol,
            csv,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt });
    }
    return datasets;
}

Contracts::StatusSnapshot BuildInitialStatusSnapshot(
    const Account::AccountInitConfig& init,
    const Config::SimulationConfig& simulation_config)
{
    // Keep the initial observable snapshot deterministic and cheap.
    Contracts::StatusSnapshot snapshot{};
    snapshot.wallet_balance = init.spot_initial_cash + init.perp_initial_wallet;
    snapshot.margin_balance = snapshot.wallet_balance;
    snapshot.available_balance = snapshot.wallet_balance;
    snapshot.perp_wallet_balance = init.perp_initial_wallet;
    snapshot.perp_margin_balance = init.perp_initial_wallet;
    snapshot.perp_available_balance = init.perp_initial_wallet;
    snapshot.spot_cash_balance = init.spot_initial_cash;
    snapshot.spot_available_balance = init.spot_initial_cash;
    snapshot.total_cash_balance = init.spot_initial_cash + init.perp_initial_wallet;
    snapshot.total_ledger_value = snapshot.total_cash_balance;
    snapshot.total_ledger_value_base = snapshot.total_cash_balance;
    snapshot.total_ledger_value_conservative = snapshot.total_cash_balance;
    snapshot.total_ledger_value_optimistic = snapshot.total_cash_balance;
    snapshot.uncertainty_band_bps = simulation_config.uncertainty_band_bps;
    snapshot.progress_pct = 0.0;
    return snapshot;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Bootstrap
