#include "FileLogger.hpp"
#include <arrow/ipc/feather.h>         // for Feather metadata (optional)
#include <arrow/io/api.h>
#include <arrow/ipc/writer.h>
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
        if (!out_res.ok()) throw std::runtime_error(out_res.status().ToString());
        auto outfile = std::move(out_res).ValueUnsafe();

        // 建立 RecordBatchWriter (Feather-V2 ≡ IPC file) 
        arrow::ipc::IpcWriteOptions write_opts = arrow::ipc::IpcWriteOptions::Defaults();
        auto w_res = arrow::ipc::MakeFileWriter(outfile.get(), s.schema, write_opts);
        if (!w_res.ok()) throw std::runtime_error(w_res.status().ToString());
        s.writer = *w_res;

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
                std::shared_ptr<arrow::RecordBatch> rb;
                auto f_res = builder.Flush(&rb);
                if (!f_res.ok())
                    throw std::runtime_error(f_res.status().message());

                auto w_res = s->writer->WriteRecordBatch(*rb);
                if (!w_res.ok())
                    throw std::runtime_error(w_res.message());

                s->rows = 0;
            }
        }

        // flush 剩余 batches 并 Close writer
        for (auto& [_, slot] : slots_) {
            std::shared_ptr<arrow::RecordBatch> rb;
            auto f_res = slot.builder->Flush(&rb);
            if (!f_res.ok())
                throw std::runtime_error(f_res.status().message());

            if (rb && rb->num_rows() > 0) {
                auto w_res = slot.writer->WriteRecordBatch(*rb);
                if (!w_res.ok())
                    throw std::runtime_error(w_res.message());
            }
            auto c_res = slot.writer->Close();
            if (!c_res.ok())
                throw std::runtime_error(c_res.message());
        }
    }
}
