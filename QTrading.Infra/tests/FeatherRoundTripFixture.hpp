#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include <arrow/table.h>

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
#include "InMemorySink.hpp"
#include "SinkLogger.hpp"

namespace fs = std::filesystem;
namespace Log = QTrading::Log;

class InfraLogFeatherRoundTripFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        tmp_dir = fs::temp_directory_path() /
            ("QTrading_InfraLogFeather_" + std::string(test_info->test_suite_name()) + "_" + test_info->name());
        fs::create_directories(tmp_dir);

        logger = std::make_shared<Log::SinkLogger>(tmp_dir.string());
        auto in_memory_sink = std::make_unique<Log::InMemorySink>();
        in_memory_sink_ = in_memory_sink.get();
        logger->AddSink(std::move(in_memory_sink));
        logger->AddSink(std::make_unique<Log::FileLogger::FeatherV2Sink>(tmp_dir.string()));
    }

    void TearDown() override
    {
        StopLogger();

        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }

    void RegisterAccountModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::Account),
            Log::FileLogger::FeatherV2::AccountLog::Schema,
            Log::FileLogger::FeatherV2::AccountLog::Serializer);
    }

    void RegisterRunMetadataModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::RunMetadata),
            Log::FileLogger::FeatherV2::RunMetadata::Schema(),
            Log::FileLogger::FeatherV2::RunMetadata::Serializer);
    }

    void RegisterPositionModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::Position),
            Log::FileLogger::FeatherV2::Position::Schema,
            Log::FileLogger::FeatherV2::Position::Serializer);
    }

    void RegisterOrderModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::Order),
            Log::FileLogger::FeatherV2::Order::Schema,
            Log::FileLogger::FeatherV2::Order::Serializer);
    }

    void RegisterMarketEventModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::MarketEvent),
            Log::FileLogger::FeatherV2::MarketEvent::Schema(),
            Log::FileLogger::FeatherV2::MarketEvent::Serializer);
    }

    void RegisterFundingEventModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::FundingEvent),
            Log::FileLogger::FeatherV2::FundingEvent::Schema(),
            Log::FileLogger::FeatherV2::FundingEvent::Serializer);
    }

    void RegisterAccountEventModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::AccountEvent),
            Log::FileLogger::FeatherV2::AccountEvent::Schema(),
            Log::FileLogger::FeatherV2::AccountEvent::Serializer);
    }

    void RegisterPositionEventModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::PositionEvent),
            Log::FileLogger::FeatherV2::PositionEvent::Schema(),
            Log::FileLogger::FeatherV2::PositionEvent::Serializer);
    }

    void RegisterOrderEventModule()
    {
        logger->RegisterModule(
            Log::LogModuleToString(Log::LogModule::OrderEvent),
            Log::FileLogger::FeatherV2::OrderEvent::Schema(),
            Log::FileLogger::FeatherV2::OrderEvent::Serializer);
    }

    void RegisterDefaultReplayModules()
    {
        RegisterAccountModule();
        RegisterPositionModule();
        RegisterOrderModule();
        RegisterAccountEventModule();
        RegisterPositionEventModule();
        RegisterOrderEventModule();
        RegisterMarketEventModule();
        RegisterFundingEventModule();
    }

    void StartLogger()
    {
        if (!logger_started_) {
            logger->Start();
            logger_started_ = true;
        }
    }

    void StopLogger()
    {
        if (logger && logger_started_) {
            logger->Stop();
            logger_started_ = false;
        }
    }

    fs::path ArrowPath(const std::string& module_name) const
    {
        return tmp_dir / (module_name + ".arrow");
    }

    Log::Logger::ModuleId ModuleId(Log::LogModule module) const
    {
        return logger ? logger->GetModuleId(Log::LogModuleToString(module)) : Log::Logger::kInvalidModuleId;
    }

    const std::vector<Log::Row>& rows() const
    {
        return in_memory_sink_->rows();
    }

    std::shared_ptr<arrow::Table> ReadArrowTable(const std::string& module_name) const
    {
        auto infile_res = arrow::io::ReadableFile::Open(ArrowPath(module_name).string());
        if (!infile_res.ok()) {
            throw std::runtime_error(infile_res.status().ToString());
        }
        auto infile = infile_res.ValueUnsafe();

        auto reader_res = arrow::ipc::RecordBatchFileReader::Open(infile);
        if (!reader_res.ok()) {
            throw std::runtime_error(reader_res.status().ToString());
        }
        auto reader = reader_res.ValueUnsafe();

        std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
        for (int i = 0; i < reader->num_record_batches(); ++i) {
            auto batch_res = reader->ReadRecordBatch(i);
            if (!batch_res.ok()) {
                throw std::runtime_error(batch_res.status().ToString());
            }
            batches.push_back(batch_res.ValueUnsafe());
        }

        auto table_res = arrow::Table::FromRecordBatches(batches);
        if (!table_res.ok()) {
            throw std::runtime_error(table_res.status().ToString());
        }
        return table_res.ValueUnsafe();
    }

    std::shared_ptr<Log::SinkLogger> logger;
    fs::path tmp_dir;

private:
    Log::InMemorySink* in_memory_sink_ = nullptr;
    bool logger_started_ = false;
};
