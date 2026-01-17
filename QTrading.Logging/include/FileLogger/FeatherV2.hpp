#pragma once

#include <arrow/api.h>
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <functional>
#include <vector>
#include "Logger.hpp"

namespace QTrading::Log {
    /// @brief Function signature for serializing log data into an Arrow RecordBatchBuilder.
    /// @param src Pointer to the log data payload.
    /// @param builder The RecordBatchBuilder used to append columns.
    using Serializer = std::function<void(const void* src, arrow::RecordBatchBuilder& builder)>;

    /// @class FeatherV2
    /// @brief A singleton logger that writes log records to Arrow IPC (Feather V2) files.
    class FeatherV2 : public Logger {
    public:
        /// @brief Constructs the FeatherV2 logger.
        /// @param dir Directory where log files will be written.
        explicit FeatherV2(const std::string& dir);

        /// @brief Registers a module with its schema and serializer.
        /// @param module    The module name (must match Logger::Log usage).
        /// @param schema    Shared pointer to the Arrow schema (column 0 = timestamp).
        /// @param serializer Function to serialize payloads into the builder.
        /// @param kind       Channel kind for the module (critical/debug).
        /// @note Must be called before Start() to keep Consume() lock-free per row.
        void RegisterModule(const std::string& module,
            std::shared_ptr<arrow::Schema> schema,
            Serializer serializer,
            ChannelKind kind = ChannelKind::Critical);

    protected:
        /// @brief Background consumer that writes Rows to Feather files.
        void Consume() override;

    private:
        FeatherV2(const FeatherV2&) = delete;            ///< Non-copyable.
        FeatherV2& operator=(const FeatherV2&) = delete; ///< Non-assignable.

        /// @brief Per-module writer state.
        struct Slot {
            std::shared_ptr<arrow::Schema>                 schema;     ///< Arrow schema.
            Serializer                                     serializer; ///< Payload serializer.
            std::unique_ptr<arrow::RecordBatchBuilder>     builder;    ///< Batch builder.
            std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;     ///< IPC file writer.
            std::shared_ptr<arrow::io::OutputStream>       outfile;    ///< Output stream.
            uint32_t                                       rows = 0;   ///< Rows in current batch.
        };
        std::vector<Slot> slots_; ///< Indexed by module id - 1.
    };
}

