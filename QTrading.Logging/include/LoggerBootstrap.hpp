#pragma once

#include "SinkLogger.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace QTrading::Log {

struct DatasetEntry {
    std::string symbol;
    std::string kline_csv;
    std::string instrument_type;
    std::optional<std::string> funding_csv;
};

struct LoggerBootstrapConfig {
    std::filesystem::path logs_root;
    std::string strategy_name;
    std::string strategy_version;
    std::string strategy_params;
    std::vector<DatasetEntry> dataset_entries;
};

struct LoggerBootstrapResult {
    std::shared_ptr<SinkLogger> logger;
    uint64_t run_id{ 0 };
    std::string dataset;
    std::filesystem::path run_dir;
};

LoggerBootstrapResult InitializeFeatherLogger(const LoggerBootstrapConfig& cfg);

} // namespace QTrading::Log

