#include "DatasetUniverseConfig.hpp"

#include <rapidjson/document.h>

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace QTrading::Service {

namespace {

std::string BuildCsvPath(const char* prefix, const std::string& symbol)
{
    return std::string(prefix) + symbol + ".csv";
}

} // namespace

SimulatorConfig LoadSimulatorConfig(const std::filesystem::path& config_path)
{
    std::ifstream in(config_path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open simulator config: " + config_path.string());
    }

    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        throw std::runtime_error("Invalid simulator config json: " + config_path.string());
    }

    const auto member = doc.FindMember("symbols");
    if (member == doc.MemberEnd() || !member->value.IsArray()) {
        throw std::runtime_error("Simulator config must contain array field 'symbols': " + config_path.string());
    }

    SimulatorConfig out;
    out.symbols.reserve(member->value.Size());
    for (const auto& item : member->value.GetArray()) {
        if (!item.IsString()) {
            throw std::runtime_error("Simulator config 'symbols' must contain only strings: " + config_path.string());
        }
        out.symbols.emplace_back(item.GetString());
    }
    if (out.symbols.empty()) {
        throw std::runtime_error("Simulator config 'symbols' must not be empty: " + config_path.string());
    }
    return out;
}

std::vector<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::SymbolDataset>
BuildSymbolDatasets(const std::vector<std::string>& raw_symbols)
{
    using SymbolDataset = QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::SymbolDataset;

    std::vector<SymbolDataset> datasets;
    datasets.reserve(raw_symbols.size() * 2);
    for (const auto& raw_symbol : raw_symbols) {
        datasets.push_back(SymbolDataset{
            raw_symbol + "_SPOT",
            BuildCsvPath(R"(\\synology\MarketData\General\MarketData\Kline\Spot\)", raw_symbol),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot
        });
        datasets.push_back(SymbolDataset{
            raw_symbol + "_PERP",
            BuildCsvPath(R"(\\synology\MarketData\General\MarketData\Kline\UsdFutures\)", raw_symbol),
            BuildCsvPath(R"(\\synology\MarketData\General\MarketData\FundingRate\UsdFutures\)", raw_symbol),
            BuildCsvPath(R"(\\synology\MarketData\General\MarketData\MarkPriceKline\UsdFutures\)", raw_symbol),
            BuildCsvPath(R"(\\synology\MarketData\General\MarketData\IndexPriceKline\UsdFutures\)", raw_symbol),
            QTrading::Dto::Trading::InstrumentType::Perp
        });
    }
    return datasets;
}

} // namespace QTrading::Service
