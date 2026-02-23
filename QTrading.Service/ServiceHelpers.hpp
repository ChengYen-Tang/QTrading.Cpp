#pragma once

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "LoggerBootstrap.hpp"
#include "Signal/FundingCarrySignalEngine.hpp"
#include "Intent/FundingCarryIntentBuilder.hpp"
#include "Risk/SimpleRiskEngine.hpp"
#include "Execution/MarketExecutionEngine.hpp"
#include "Monitoring/SimpleMonitoring.hpp"

#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace QTrading::Service::Helpers {

std::string JsonEscape(const std::string& input);
std::string Utf8Path(const char8_t* path);

void SetEnvVar(const char* key, const std::string& value);
void InstallSignalHandlers();
bool StopRequested();
std::filesystem::path ResolveRepoRelativePath(
    const std::filesystem::path& source_file_path,
    const std::filesystem::path& repo_relative_path);

std::string InstrumentTypeToString(std::optional<QTrading::Dto::Trading::InstrumentType> type);
QTrading::Log::LoggerBootstrapConfig BuildLoggerBootstrapConfig(
    const std::filesystem::path& logs_root,
    const std::vector<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange::SymbolDataset>& symbol_csv,
    const std::string& strategy_name,
    const std::string& strategy_version,
    const std::string& strategy_params);
void LoadFundingCarryConfig(
    const std::filesystem::path& config_path,
    QTrading::Signal::FundingCarrySignalEngine::Config& signal_cfg,
    QTrading::Intent::FundingCarryIntentBuilder::Config& intent_cfg,
    QTrading::Risk::SimpleRiskEngine::Config& risk_cfg,
    QTrading::Execution::MarketExecutionEngine::Config& execution_cfg,
    QTrading::Monitoring::SimpleMonitoring::Config& monitoring_cfg);
void EmitExchangeStatusLine(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange);

} // namespace QTrading::Service::Helpers
