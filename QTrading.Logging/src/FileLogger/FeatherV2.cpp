#include "FileLogger/FeatherV2.hpp"
#include <arrow/ipc/feather.h>   ///< Feather metadata support.
#include <arrow/util/key_value_metadata.h>
#include "parquet/stream_writer.h"
#include <stdexcept>
#include <filesystem>

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

namespace QTrading::Log {

    /// @brief Construct a FeatherV2 logger using the specified directory.
    /// @param dir Directory for output files.
    FeatherV2::FeatherV2(const std::string& dir)
        : Logger(dir) {
    }

    /// @brief Register a module's Arrow schema and serializer.
    /// Must be called once before any logs for that module.
    /// @param module    Name matching Logger::Log.
    /// @param schema    Arrow schema (col 0 = timestamp).
    /// @param serializer Function to serialize payload into builder.
    void FeatherV2::RegisterModule(const std::string& module,
        std::shared_ptr<arrow::Schema> schema,
        Serializer serializer,
        ChannelKind kind)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            throw std::runtime_error("RegisterModule must be called before Start().");
        }
        if (GetModuleId(module) != kInvalidModuleId) {
            throw std::runtime_error("Module already registered: " + module);
        }
        const auto module_id = RegisterModuleId(module, kind);

        Slot s;
        s.schema = WithSchemaMetadata(schema, module);
        s.serializer = std::move(serializer);

        // Build the RecordBatchBuilder (capacity 8192 rows).
        auto res = arrow::RecordBatchBuilder::Make(
            s.schema, arrow::default_memory_pool(), /*capacity=*/8192);
        if (!res.ok()) {
            throw std::runtime_error(res.status().ToString());
        }
        s.builder = std::move(*res);

        // Prepare the IPC output file.
        fs::path log_path = fs::path(dir) / (module + ".arrow");
        auto out_res = arrow::io::FileOutputStream::Open(
            log_path.string(), /*truncate=*/true);
        PARQUET_ASSIGN_OR_THROW(auto outfile, out_res);
        s.outfile = std::move(outfile);

        // Create the IPC writer.
        arrow::ipc::IpcWriteOptions write_opts = arrow::ipc::IpcWriteOptions::Defaults();
        PARQUET_ASSIGN_OR_THROW(auto w_res,
            arrow::ipc::MakeFileWriter(s.outfile, s.schema, write_opts));
        s.writer = w_res;

        if (slots_.size() < module_id) {
            slots_.resize(module_id);
        }
        slots_.at(static_cast<size_t>(module_id - 1)) = std::move(s);
    }

    /// @brief Background loop to write Rows into Feather files.
    /// Exits when channel is closed and empty.
    void FeatherV2::Consume()
    {
        constexpr size_t kMaxBatch = 1024;
        std::vector<Row> batch;

        // Read until all channels are closed and drained.
        while (WaitForBatch(batch, kMaxBatch)) {
            for (auto& row : batch) {
                auto* s = &slots_.at(static_cast<size_t>(row.module_id - 1));
                auto& builder = *s->builder;

                // Column 0: timestamp
                PARQUET_THROW_NOT_OK(builder.GetFieldAs<arrow::UInt64Builder>(0)->Append(row.ts));

                // Remaining columns via serializer.
                s->serializer(row.payload.get(), builder);

                // Flush when batch capacity reached.
                if (++s->rows >= 8192) {
                    auto rb_res = builder.Flush();
                    PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
                    PARQUET_THROW_NOT_OK(s->writer->WriteRecordBatch(*rb));
                    IncrementFlushCount();
                    s->rows = 0;
                }
            }
        }

        // Final flush & close all writers.
        for (auto& slot : slots_) {
            auto rb_res = slot.builder->Flush();
            PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
            if (rb && rb->num_rows() > 0) {
                PARQUET_THROW_NOT_OK(slot.writer->WriteRecordBatch(*rb));
                IncrementFlushCount();
            }
            PARQUET_THROW_NOT_OK(slot.writer->Close());
            PARQUET_THROW_NOT_OK(slot.outfile->Close());
        }
    }
}
