#include "Logger.hpp"
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
using QTrading::Utils::Queue::ChannelFactory;

namespace QTrading::Log {

    /// @brief Create logger and ensure the directory exists.
    /// @param dir Directory for log files.
    Logger::Logger(const std::string& dir)
        : dir(dir)
    {
        fs::create_directories(dir);
    }

    Logger::ModuleId Logger::RegisterModuleId(const std::string& module)
    {
        return RegisterModuleId(module, ChannelKind::Critical);
    }

    Logger::ModuleId Logger::RegisterModuleId(const std::string& module, ChannelKind kind)
    {
        if (channel) {
            throw std::runtime_error("RegisterModuleId must be called before Start().");
        }
        auto it = module_ids_.find(module);
        if (it != module_ids_.end()) {
            const auto id = it->second;
            if (id > 0 && id <= module_kinds_.size() && module_kinds_[id - 1] != kind) {
                throw std::runtime_error("Module already registered with different channel kind: " + module);
            }
            return id;
        }
        const auto id = static_cast<ModuleId>(module_ids_.size() + 1);
        module_ids_.emplace(module, id);
        if (module_kinds_.size() < id) {
            module_kinds_.resize(id, ChannelKind::Critical);
        }
        module_kinds_[id - 1] = kind;
        return id;
    }

    Logger::ModuleId Logger::GetModuleId(const std::string& module) const
    {
        auto it = module_ids_.find(module);
        if (it == module_ids_.end()) {
            return kInvalidModuleId;
        }
        return it->second;
    }

    Logger::MetricsSnapshot Logger::GetMetrics() const
    {
        MetricsSnapshot out;
        out.enqueue_ok = enqueue_ok_.load(std::memory_order_relaxed);
        out.enqueue_fail = enqueue_fail_.load(std::memory_order_relaxed);
        out.flush_count = flush_count_.load(std::memory_order_relaxed);
        if (channel) {
            out.queue_depth = channel->Size();
            out.drop = channel->DropCount();
        }
        if (debug_channel_) {
            out.queue_depth += debug_channel_->Size();
            out.drop += debug_channel_->DropCount();
        }
        return out;
    }

    void Logger::IncrementFlushCount(uint64_t count)
    {
        flush_count_.fetch_add(count, std::memory_order_relaxed);
    }

    bool Logger::WaitForBatch(std::vector<Row>& out, size_t max_items)
    {
        out.clear();
        out.reserve(max_items);
        auto drain = [&](const std::shared_ptr<QTrading::Utils::Queue::Channel<Row>>& ch, size_t remaining) {
            if (!ch || remaining == 0) {
                return;
            }
            auto batch = ch->ReceiveMany(remaining);
            for (auto& r : batch) {
                out.push_back(std::move(r));
            }
        };

        while (true) {
            drain(channel, max_items);
            if (debug_channel_) {
                drain(debug_channel_, max_items - out.size());
            }
            if (!out.empty()) {
                return true;
            }

            const bool critical_closed = !channel || channel->IsClosed();
            const bool debug_closed = !debug_channel_ || debug_channel_->IsClosed();
            const bool critical_empty = !channel || channel->Size() == 0;
            const bool debug_empty = !debug_channel_ || debug_channel_->Size() == 0;
            if (critical_closed && debug_closed && critical_empty && debug_empty) {
                return false;
            }

            std::unique_lock<std::mutex> lk(consume_mtx_);
            consume_cv_.wait(lk, [&] {
                const bool critical_has = channel && channel->Size() > 0;
                const bool debug_has = debug_channel_ && debug_channel_->Size() > 0;
                if (critical_has || debug_has) {
                    return true;
                }
                const bool critical_closed = !channel || channel->IsClosed();
                const bool debug_closed = !debug_channel_ || debug_channel_->IsClosed();
                return critical_closed && debug_closed;
                });
        }
    }

    /// @brief Start the consumer thread and initialize the channel.
    /// No-op if already started.
    void Logger::Start()
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            return;
        }
        QTrading::Utils::Queue::ChannelOptions options;
        options.single_reader = true;
        channel = ChannelFactory::CreateUnboundedChannel<Row>(options);
        debug_channel_.reset();
        consumer = boost::thread(&Logger::Consume, this);
    }

    /// @brief Start the consumer thread and initialize a bounded channel.
    /// No-op if already started.
    void Logger::Start(size_t capacity, QTrading::Utils::Queue::OverflowPolicy policy)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            return;
        }
        channel = ChannelFactory::CreateBoundedChannel<Row>(capacity, policy);
        debug_channel_.reset();
        consumer = boost::thread(&Logger::Consume, this);
    }

    void Logger::StartWithDebugChannel(size_t debug_capacity, QTrading::Utils::Queue::OverflowPolicy policy)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            return;
        }
        QTrading::Utils::Queue::ChannelOptions options;
        options.single_reader = true;
        channel = ChannelFactory::CreateUnboundedChannel<Row>(options);
        debug_channel_ = ChannelFactory::CreateBoundedChannel<Row>(debug_capacity, policy);
        consumer = boost::thread(&Logger::Consume, this);
    }

    /// @brief Stop the consumer thread, close channel, and join.
    /// No-op if not started.
    void Logger::Stop()
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!channel) {
                return;
            }
            channel->Close();
            if (debug_channel_) {
                debug_channel_->Close();
            }
        }
        consume_cv_.notify_all();
        consumer.join();
        channel.reset();
        debug_channel_.reset();
    }
}
