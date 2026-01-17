#include "FileLogger/FeatherV2Sink.hpp"

#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/feather.h>
#include <arrow/util/key_value_metadata.h>
#include "parquet/stream_writer.h"
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::shared_ptr<arrow::Schema> WithSchemaMetadata(const std::shared_ptr<arrow::Schema>& schema,
    const std::string& module)
{
    std::shared_ptr<arrow::KeyValueMetadata> meta =
        schema->metadata() ? schema->metadata()->Copy()
                           : std::make_shared<arrow::KeyValueMetadata>();
    if (meta->FindKey("schema_version") < 0) {
        meta->Append("schema_version", "1");
    }
    if (meta->FindKey("module") < 0) {
        meta->Append("module", module);
    }
    return schema->WithMetadata(meta);
}

} // namespace

namespace QTrading::Log::FileLogger {

    FeatherV2Sink::FeatherV2Sink(const std::string& dir)
        : dir_(dir)
    {
        fs::create_directories(dir_);
    }

    void FeatherV2Sink::RegisterModule(Logger::ModuleId module_id,
        const std::string& module,
        const std::shared_ptr<arrow::Schema>& schema,
        const Serializer& serializer)
    {
        if (module_id == Logger::kInvalidModuleId) {
            throw std::runtime_error("Invalid module id for module: " + module);
        }
        Slot s;
        s.schema = WithSchemaMetadata(schema, module);
        s.serializer = serializer;

        auto res = arrow::RecordBatchBuilder::Make(
            s.schema, arrow::default_memory_pool(), /*capacity=*/8192);
        if (!res.ok()) {
            throw std::runtime_error(res.status().ToString());
        }
        s.builder = std::move(*res);

        fs::path log_path = fs::path(dir_) / (module + ".arrow");
        auto out_res = arrow::io::FileOutputStream::Open(
            log_path.string(), /*truncate=*/true);
        PARQUET_ASSIGN_OR_THROW(auto outfile, out_res);
        s.outfile = std::move(outfile);

        arrow::ipc::IpcWriteOptions write_opts = arrow::ipc::IpcWriteOptions::Defaults();
        PARQUET_ASSIGN_OR_THROW(auto w_res,
            arrow::ipc::MakeFileWriter(s.outfile, s.schema, write_opts));
        s.writer = w_res;

        if (slots_.size() < module_id) {
            slots_.resize(module_id);
        }
        slots_.at(static_cast<size_t>(module_id - 1)) = std::move(s);
    }

    uint64_t FeatherV2Sink::WriteRow(const Row& row)
    {
        auto& s = slots_.at(static_cast<size_t>(row.module_id - 1));
        auto& builder = *s.builder;

        PARQUET_THROW_NOT_OK(builder.GetFieldAs<arrow::UInt64Builder>(0)->Append(row.ts));
        s.serializer(row.payload.get(), builder);

        if (++s.rows >= 8192) {
            auto rb_res = builder.Flush();
            PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
            PARQUET_THROW_NOT_OK(s.writer->WriteRecordBatch(*rb));
            s.rows = 0;
            return 1;
        }
        return 0;
    }

    uint64_t FeatherV2Sink::Flush()
    {
        uint64_t flushes = 0;
        for (auto& slot : slots_) {
            auto rb_res = slot.builder->Flush();
            PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
            if (rb && rb->num_rows() > 0) {
                PARQUET_THROW_NOT_OK(slot.writer->WriteRecordBatch(*rb));
                ++flushes;
            }
            slot.rows = 0;
        }
        return flushes;
    }

    void FeatherV2Sink::Close()
    {
        for (auto& slot : slots_) {
            PARQUET_THROW_NOT_OK(slot.writer->Close());
            PARQUET_THROW_NOT_OK(slot.outfile->Close());
        }
    }

} // namespace QTrading::Log::FileLogger
