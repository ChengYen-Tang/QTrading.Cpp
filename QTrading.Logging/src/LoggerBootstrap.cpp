#include "LoggerBootstrap.hpp"

#include "Enum/LogModule.hpp"
#include "FileLogger/FeatherV2/AccountEvent.hpp"
#include "FileLogger/FeatherV2/AccountLog.hpp"
#include "FileLogger/FeatherV2/FundingEvent.hpp"
#include "FileLogger/FeatherV2/MarketEvent.hpp"
#include "FileLogger/FeatherV2/Order.hpp"
#include "FileLogger/FeatherV2/OrderEvent.hpp"
#include "FileLogger/FeatherV2/Position.hpp"
#include "FileLogger/FeatherV2/PositionEvent.hpp"
#include "FileLogger/FeatherV2/RunMetadata.hpp"
#include "FileLogger/FeatherV2Sink.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace QTrading::Log {

namespace {

uint64_t BuildRunIdNowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

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

std::string BuildDatasetDescriptor(const std::vector<DatasetEntry>& dataset_entries)
{
    std::string dataset;
    for (const auto& entry : dataset_entries) {
        if (!dataset.empty()) {
            dataset += ";";
        }
        dataset += entry.symbol;
        dataset += "=";
        dataset += entry.kline_csv;
        dataset += "@";
        dataset += entry.instrument_type;
        if (entry.funding_csv.has_value() && !entry.funding_csv->empty()) {
            dataset += "|";
            dataset += *entry.funding_csv;
        }
    }
    return dataset;
}

std::filesystem::path BuildRunDirectory(const std::filesystem::path& logs_root)
{
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &now_time);
    std::ostringstream run_dir_name;
    run_dir_name << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return logs_root / run_dir_name.str();
}

void WriteRunMetadataFiles(
    const std::filesystem::path& run_dir,
    uint64_t run_id,
    const std::string& strategy_name,
    const std::string& strategy_version,
    const std::string& strategy_params,
    const std::string& dataset)
{
    {
        std::ofstream meta((run_dir / "run_metadata.json").string(), std::ios::out | std::ios::trunc);
        if (meta) {
            meta << "{\n"
                 << "  \"run_id\": " << run_id << ",\n"
                 << "  \"strategy_name\": \"" << JsonEscape(strategy_name) << "\",\n"
                 << "  \"strategy_version\": \"" << JsonEscape(strategy_version) << "\",\n"
                 << "  \"strategy_params\": \"" << JsonEscape(strategy_params) << "\",\n"
                 << "  \"dataset\": \"" << JsonEscape(dataset) << "\"\n"
                 << "}\n";
        }
    }
    {
        std::ofstream dataset_file((run_dir / "dataset_paths.json").string(), std::ios::out | std::ios::trunc);
        if (dataset_file) {
            dataset_file << "{\n"
                         << "  \"dataset\": \"" << JsonEscape(dataset) << "\"\n"
                         << "}\n";
        }
    }
}

void RegisterDefaultModules(SinkLogger& logger)
{
    logger.RegisterModule(
        LogModuleToString(LogModule::Account),
        FileLogger::FeatherV2::AccountLog::Schema,
        FileLogger::FeatherV2::AccountLog::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::Position),
        FileLogger::FeatherV2::Position::Schema,
        FileLogger::FeatherV2::Position::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::Order),
        FileLogger::FeatherV2::Order::Schema,
        FileLogger::FeatherV2::Order::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::AccountEvent),
        FileLogger::FeatherV2::AccountEvent::Schema(),
        FileLogger::FeatherV2::AccountEvent::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::PositionEvent),
        FileLogger::FeatherV2::PositionEvent::Schema(),
        FileLogger::FeatherV2::PositionEvent::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::OrderEvent),
        FileLogger::FeatherV2::OrderEvent::Schema(),
        FileLogger::FeatherV2::OrderEvent::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::MarketEvent),
        FileLogger::FeatherV2::MarketEvent::Schema(),
        FileLogger::FeatherV2::MarketEvent::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::FundingEvent),
        FileLogger::FeatherV2::FundingEvent::Schema(),
        FileLogger::FeatherV2::FundingEvent::Serializer);
    logger.RegisterModule(
        LogModuleToString(LogModule::RunMetadata),
        FileLogger::FeatherV2::RunMetadata::Schema(),
        FileLogger::FeatherV2::RunMetadata::Serializer);
}

} // namespace

LoggerBootstrapResult InitializeFeatherLogger(const LoggerBootstrapConfig& cfg)
{
    LoggerBootstrapResult out;
    out.run_id = BuildRunIdNowMs();
    out.dataset = BuildDatasetDescriptor(cfg.dataset_entries);
    out.run_dir = BuildRunDirectory(cfg.logs_root);
    std::filesystem::create_directories(out.run_dir);

    out.logger = std::make_shared<SinkLogger>(out.run_dir.string());
    out.logger->AddSink(std::make_unique<FileLogger::FeatherV2Sink>(out.run_dir.string()));

    WriteRunMetadataFiles(
        out.run_dir,
        out.run_id,
        cfg.strategy_name,
        cfg.strategy_version,
        cfg.strategy_params,
        out.dataset);

    RegisterDefaultModules(*out.logger);
    out.logger->Start();

    FileLogger::FeatherV2::RunMetadataDto meta{};
    meta.run_id = out.run_id;
    meta.strategy_name = cfg.strategy_name;
    meta.strategy_version = cfg.strategy_version;
    meta.strategy_params = cfg.strategy_params;
    meta.dataset = out.dataset;
    out.logger->Log(LogModuleToString(LogModule::RunMetadata), std::move(meta));
    return out;
}

} // namespace QTrading::Log
