#include "FileLogger.hpp"
#include <arrow/ipc/feather.h>         // for Feather metadata (optional)
#include "parquet/stream_writer.h"
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
using QTrading::Utils::Queue::ChannelFactory;

namespace QTrading::Log {
	FileLogger::FileLogger(const std::string& dir)
		: dir(dir)
	{
        fs::create_directories(dir);
	}

    /* 註冊模組 */
    void FileLogger::RegisterModule(const std::string& module,
        std::shared_ptr<arrow::Schema> schema,
        Serializer serializer)
    {
        std::lock_guard lk(mtx_);
        if (slots_.count(module))
            throw std::runtime_error("Module already registered: " + module);

        Slot s;
        s.schema = std::move(schema);
        s.serializer = std::move(serializer);

        // 構建 RecordBatchBuilder 
        auto res = arrow::RecordBatchBuilder::Make(
            s.schema, arrow::default_memory_pool(), /*capacity=*/8192);
        if (!res.ok()) throw std::runtime_error(res.status().ToString());
        s.builder = std::move(*res);

        // 準備 Feather-V2 (IPC) 檔案
		fs::path log_path = fs::path(dir) / (module + ".arrow");
        auto out_res = arrow::io::FileOutputStream::Open(
            log_path.string(),
            /*truncate=*/true);
        PARQUET_ASSIGN_OR_THROW(auto outfile, out_res);
        s.outfile = std::move(outfile);

        // 建立 RecordBatchWriter (Feather-V2 ≡ IPC file) 
        arrow::ipc::IpcWriteOptions write_opts = arrow::ipc::IpcWriteOptions::Defaults();
        PARQUET_ASSIGN_OR_THROW(auto w_res,
            arrow::ipc::MakeFileWriter(s.outfile,       // 直接傳 shared_ptr
                s.schema,
                write_opts));
        s.writer = w_res;

        slots_.emplace(module, std::move(s));
    }

    /* 啟動 Consumer */
    void FileLogger::Start()
    {
        std::lock_guard lk(mtx_);
        if (channel_) return;  // already started

        channel_.reset(ChannelFactory::CreateUnboundedChannel<Row>());
        consumer_ = boost::thread(&FileLogger::Consume, this);
    }

    /* 停止 Consumer */
    void FileLogger::Stop()
    {
        {
            std::lock_guard lk(mtx_);
            if (!channel_) return;
            channel_->Close();
        }
        consumer_.join();
        channel_.reset();
    }

    /* Consumer loop：將一筆筆 Row 寫入 Feather-V2 檔案 */
    void FileLogger::Consume()
    {
        while (true) {
            auto opt = channel_->Receive();
            if (!opt) break;  // Channel 关闭且空

            Row row = std::move(*opt);
            Slot* s;
            {
                std::lock_guard lk(mtx_);
                s = &slots_.at(row.module);
            }
            auto& builder = *s->builder;

            // 第 0 列：Timestamp
            builder.GetFieldAs<arrow::UInt64Builder>(0)->Append(row.ts);

            // 其余列由注册的 serializer 填充
            s->serializer(row.payload.get(), builder);

            // 达到阈值就 Flush + Write
            if (++s->rows >= 8192) {
                auto rb_res = builder.Flush();
                PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);

                auto w_res = s->writer->WriteRecordBatch(*rb);
                PARQUET_THROW_NOT_OK(w_res);

                s->rows = 0;
            }
        }

        // flush 剩余 batches 并 Close writer
        for (auto& [_, slot] : slots_) {
            auto rb_res = slot.builder->Flush();
            PARQUET_ASSIGN_OR_THROW(auto rb, rb_res);

            if (rb && rb->num_rows() > 0) {
                auto w_res = slot.writer->WriteRecordBatch(*rb);
                PARQUET_THROW_NOT_OK(w_res);
            }
            auto c_res = slot.writer->Close();
            PARQUET_THROW_NOT_OK(c_res);

            auto s_res = slot.outfile->Close();
            PARQUET_THROW_NOT_OK(s_res);
        }
    }
}
