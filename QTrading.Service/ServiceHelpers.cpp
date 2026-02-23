#include "ServiceHelpers.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace QTrading::Service::Helpers {

namespace {

std::atomic<bool> g_stop_requested{ false };

void HandleSignal(int)
{
    g_stop_requested.store(true, std::memory_order_relaxed);
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
    for (size_t i = 0; i < snap.prices.size(); ++i) {
        const auto& p = snap.prices[i];
        if (i > 0) {
            oss << ",";
        }
        oss << p.symbol << "=";
        if (p.has_price) {
            oss << p.price;
        }
        else {
            oss << "n/a";
        }
    }
    std::cout << oss.str() << std::endl;
}

} // namespace QTrading::Service::Helpers
