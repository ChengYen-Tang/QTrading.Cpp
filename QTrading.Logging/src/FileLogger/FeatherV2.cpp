#include "FileLogger/FeatherV2.hpp"
#include <arrow/ipc/feather.h>   ///< Feather metadata support.
#include "parquet/stream_writer.h"
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

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
        Serializer serializer)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (slots_.count(module)) {
            throw std::runtime_error("Module already registered: " + module);
        }

        Slot s;
        s.schema = std::move(schema);
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

        slots_.emplace(module, std::move(s));
    }

    /// @brief Background loop to write Rows into Feather files.
    /// Exits when channel is closed and empty.
    void FeatherV2::Consume()
    {
        constexpr size_t kMaxBatch = 1024;

        // Read until no more Rows.
        while (true) {
            auto batch = channel->ReceiveMany(kMaxBatch);
            if (batch.empty()) {
                // Channel closed & drained.
                if (channel->IsClosed()) break;

                // Avoid tight spin when empty but not closed.
                auto opt = channel->Receive();
                if (!opt) break;
                batch.push_back(std::move(*opt));
            }

            for (auto& row : batch) {
                Slot* s;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    s = &slots_.at(row.module);
                }
                auto& builder = *s->builder;

                // Column 0: timestamp
                builder.GetFieldAs<arrow::UInt64Builder>(0)->Append(row.ts);

                // Remaining columns via serializer.
                s->serializer(row.payload.get(), builder);

                // Flush when batch capacity reached.
                if (++s->rows >= 8192) {
                    auto rb_res = builder.Flush();
                    PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
                    PARQUET_THROW_NOT_OK(s->writer->WriteRecordBatch(*rb));
                    s->rows = 0;
                }
            }
        }

        // Final flush & close all writers.
        for (auto& [_, slot] : slots_) {
            auto rb_res = slot.builder->Flush();
            PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);
            if (rb && rb->num_rows() > 0) {
                PARQUET_THROW_NOT_OK(slot.writer->WriteRecordBatch(*rb));
            }
            PARQUET_THROW_NOT_OK(slot.writer->Close());
            PARQUET_THROW_NOT_OK(slot.outfile->Close());
        }
    }
}
