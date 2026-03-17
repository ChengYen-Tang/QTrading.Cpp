#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

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
#include "InMemorySink.hpp"
#include "SinkLogger.hpp"

namespace fs = std::filesystem;
namespace Log = QTrading::Log;

class InMemorySinkInjection {
public:
    std::unique_ptr<Log::InMemorySink> CreateSink()
    {
        auto sink = std::make_unique<Log::InMemorySink>();
        sink_ = sink.get();
        return sink;
    }

    const std::vector<Log::Row>& rows() const
    {
        return sink_->rows();
    }

    bool attached() const
    {
        return sink_ != nullptr;
    }

private:
    Log::InMemorySink* sink_ = nullptr;
};

class ModuleIdResolver {
public:
    void Register(Log::Logger::ModuleId module_id, std::string module_name)
    {
        by_id_[module_id] = std::move(module_name);
    }

    std::optional<std::string> Resolve(Log::Logger::ModuleId module_id) const
    {
        const auto it = by_id_.find(module_id);
        if (it == by_id_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::unordered_map<Log::Logger::ModuleId, std::string> by_id_;
};

struct ArrivedRowView {
    size_t arrival_index{};
    const Log::Row* row{};
};

struct LegacyLogContractSnapshot {
    std::string module_name;
    Log::Logger::ModuleId module_id{ Log::Logger::kInvalidModuleId };
    uint64_t ts{ 0 };
    uint64_t run_id{ 0 };
    uint64_t step_seq{ 0 };
    uint64_t event_seq{ 0 };
    std::string symbol;

    bool operator==(const LegacyLogContractSnapshot& other) const
    {
        return module_name == other.module_name &&
            module_id == other.module_id &&
            ts == other.ts &&
            run_id == other.run_id &&
            step_seq == other.step_seq &&
            event_seq == other.event_seq &&
            symbol == other.symbol;
    }
};

inline std::ostream& operator<<(std::ostream& os, const LegacyLogContractSnapshot& snapshot)
{
    os << "{module=" << snapshot.module_name
       << ", id=" << snapshot.module_id
       << ", ts=" << snapshot.ts
       << ", run_id=" << snapshot.run_id
       << ", step_seq=" << snapshot.step_seq
       << ", event_seq=" << snapshot.event_seq
       << ", symbol=" << snapshot.symbol
       << "}";
    return os;
}

class InfraLogTestFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        tmp_dir = fs::temp_directory_path() /
            ("QTrading_InfraLog_" + std::string(test_info->test_suite_name()) + "_" + test_info->name());
        fs::create_directories(tmp_dir);

        logger = std::make_shared<Log::SinkLogger>(tmp_dir.string());

        logger->AddSink(sink_injection.CreateSink());

        RegisterDefaultModules(*logger, module_id_resolver);
        logger->Start();
        logger_started_ = true;
    }

    void TearDown() override
    {
        StopLogger();

        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }

    void StopLogger()
    {
        if (logger && logger_started_) {
            logger->Stop();
            logger_started_ = false;
        }
    }

    const std::vector<Log::Row>& rows() const
    {
        return sink_injection.rows();
    }

    Log::Logger::ModuleId ModuleId(Log::LogModule module) const
    {
        return logger ? logger->GetModuleId(Log::LogModuleToString(module)) : Log::Logger::kInvalidModuleId;
    }

    std::optional<std::string> ResolveModuleName(Log::Logger::ModuleId module_id) const
    {
        return module_id_resolver.Resolve(module_id);
    }

    template <typename T>
    const T* RowPayloadCast(const Log::Row* row) const
    {
        if (!row) {
            return nullptr;
        }
        if (row->module_id == Log::Logger::kInvalidModuleId) {
            return nullptr;
        }
        if (!row->payload || row->payload.get() == nullptr) {
            return nullptr;
        }
        return static_cast<const T*>(row->payload.get());
    }

    std::vector<ArrivedRowView> DrainAndSortRowsByArrival() const
    {
        std::vector<ArrivedRowView> out;
        const auto& captured_rows = rows();
        out.reserve(captured_rows.size());
        for (size_t i = 0; i < captured_rows.size(); ++i) {
            out.push_back(ArrivedRowView{ i, &captured_rows[i] });
        }
        return out;
    }

    std::vector<ArrivedRowView> FilterRowsByModule(Log::Logger::ModuleId module_id) const
    {
        std::vector<ArrivedRowView> out;
        for (const auto& row_view : DrainAndSortRowsByArrival()) {
            if (row_view.row && row_view.row->module_id == module_id) {
                out.push_back(row_view);
            }
        }
        return out;
    }

    std::vector<ArrivedRowView> FilterRowsByModule(Log::LogModule module) const
    {
        return FilterRowsByModule(ModuleId(module));
    }

    template <typename T>
    void AssertSingleStepEnvelope(
        const std::vector<ArrivedRowView>& row_views,
        uint64_t expected_run_id,
        uint64_t expected_step_seq,
        uint64_t expected_ts_exchange,
        std::optional<uint64_t> expected_first_event_seq = std::nullopt) const
    {
        ASSERT_FALSE(row_views.empty());

        uint64_t first_event_seq = 0;
        bool first = true;
        for (size_t i = 0; i < row_views.size(); ++i) {
            ASSERT_NE(row_views[i].row, nullptr);
            const auto* payload = RowPayloadCast<T>(row_views[i].row);
            ASSERT_NE(payload, nullptr);

            EXPECT_EQ(row_views[i].row->ts, expected_ts_exchange);
            EXPECT_EQ(payload->run_id, expected_run_id);
            EXPECT_EQ(payload->step_seq, expected_step_seq);

            if (first) {
                first_event_seq = expected_first_event_seq.value_or(payload->event_seq);
                first = false;
            }
            EXPECT_EQ(payload->event_seq, first_event_seq + i);
        }
    }

    std::optional<LegacyLogContractSnapshot> BuildLegacyLogContractSnapshot(const Log::Row* row) const
    {
        return BuildLegacyLogContractSnapshot(module_id_resolver, row);
    }

    static std::optional<LegacyLogContractSnapshot> BuildLegacyLogContractSnapshot(
        const ModuleIdResolver& resolver,
        const Log::Row* row)
    {
        if (!row || row->module_id == Log::Logger::kInvalidModuleId) {
            return std::nullopt;
        }

        const auto module_name = resolver.Resolve(row->module_id);
        if (!module_name.has_value()) {
            return std::nullopt;
        }

        LegacyLogContractSnapshot snapshot{};
        snapshot.module_name = *module_name;
        snapshot.module_id = row->module_id;
        snapshot.ts = row->ts;

        if (*module_name == Log::LogModuleToString(Log::LogModule::RunMetadata)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::RunMetadataDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.symbol.clear();
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::MarketEvent)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::MarketEventDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.step_seq = payload->step_seq;
            snapshot.event_seq = payload->event_seq;
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::FundingEvent)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::FundingEventDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.step_seq = payload->step_seq;
            snapshot.event_seq = payload->event_seq;
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::AccountEvent)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::AccountEventDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.step_seq = payload->step_seq;
            snapshot.event_seq = payload->event_seq;
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::PositionEvent)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::PositionEventDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.step_seq = payload->step_seq;
            snapshot.event_seq = payload->event_seq;
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::OrderEvent)) {
            const auto* payload = static_cast<const Log::FileLogger::FeatherV2::OrderEventDto*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.run_id = payload->run_id;
            snapshot.step_seq = payload->step_seq;
            snapshot.event_seq = payload->event_seq;
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::Account)) {
            const auto* payload = static_cast<const QTrading::dto::AccountLog*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            (void)payload;
            snapshot.symbol.clear();
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::Position)) {
            const auto* payload = static_cast<const QTrading::dto::Position*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        if (*module_name == Log::LogModuleToString(Log::LogModule::Order)) {
            const auto* payload = static_cast<const QTrading::dto::Order*>(row->payload.get());
            if (!payload) {
                return std::nullopt;
            }
            snapshot.symbol = payload->symbol;
            return snapshot;
        }

        return std::nullopt;
    }

    std::shared_ptr<Log::SinkLogger> logger;
    InMemorySinkInjection sink_injection;
    ModuleIdResolver module_id_resolver;
    fs::path tmp_dir;

    static void RegisterModule(
        Log::SinkLogger& logger,
        ModuleIdResolver& resolver,
        const std::string& module_name,
        std::shared_ptr<arrow::Schema> schema,
        Log::Serializer serializer)
    {
        logger.RegisterModule(module_name, std::move(schema), serializer);
        resolver.Register(logger.GetModuleId(module_name), module_name);
    }

    static void RegisterDefaultModules(Log::SinkLogger& logger, ModuleIdResolver& resolver)
    {
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::Account),
            Log::FileLogger::FeatherV2::AccountLog::Schema,
            Log::FileLogger::FeatherV2::AccountLog::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::Position),
            Log::FileLogger::FeatherV2::Position::Schema,
            Log::FileLogger::FeatherV2::Position::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::Order),
            Log::FileLogger::FeatherV2::Order::Schema,
            Log::FileLogger::FeatherV2::Order::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::AccountEvent),
            Log::FileLogger::FeatherV2::AccountEvent::Schema(),
            Log::FileLogger::FeatherV2::AccountEvent::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::PositionEvent),
            Log::FileLogger::FeatherV2::PositionEvent::Schema(),
            Log::FileLogger::FeatherV2::PositionEvent::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::OrderEvent),
            Log::FileLogger::FeatherV2::OrderEvent::Schema(),
            Log::FileLogger::FeatherV2::OrderEvent::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::MarketEvent),
            Log::FileLogger::FeatherV2::MarketEvent::Schema(),
            Log::FileLogger::FeatherV2::MarketEvent::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::FundingEvent),
            Log::FileLogger::FeatherV2::FundingEvent::Schema(),
            Log::FileLogger::FeatherV2::FundingEvent::Serializer);
        RegisterModule(
            logger,
            resolver,
            Log::LogModuleToString(Log::LogModule::RunMetadata),
            Log::FileLogger::FeatherV2::RunMetadata::Schema(),
            Log::FileLogger::FeatherV2::RunMetadata::Serializer);
    }

private:
    bool logger_started_ = false;
};
