#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/thread.hpp>
#include "Global.hpp"
#include "LogPayload.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Log {
    /// @struct Row
    /// @brief Represents one log record for output.
    struct Row {
        using ModuleId = uint32_t;
        ModuleId              module_id; ///< Numeric module identifier.
        unsigned long long    ts;        ///< Timestamp from GlobalTimestamp.
        PayloadPtr            payload;   ///< Pointer to the log data object.
    };

    /// @class Logger
    /// @brief Abstract base for a singleton logger with a background consumer thread.
    class Logger {
    public:
        using ModuleId = Row::ModuleId;
        static constexpr ModuleId kInvalidModuleId = 0;

        enum class ChannelKind : uint8_t {
            Critical,
            Debug
        };

        struct MetricsSnapshot {
            uint64_t enqueue_ok = 0;
            uint64_t enqueue_fail = 0;
            uint64_t drop = 0;
            uint64_t queue_depth = 0;
            uint64_t flush_count = 0;
        };

        /// @brief Create a logger storing output in the given directory.
        /// @param dir Directory path for log files.
        explicit Logger(const std::string& dir);

        /// @brief Start the consumer thread with an unbounded channel.
        virtual void Start();

        /// @brief Start the consumer thread with a bounded channel.
        /// @param capacity Maximum number of queued log rows.
        /// @param policy Overflow policy when the queue is full.
        virtual void Start(size_t capacity,
            QTrading::Utils::Queue::OverflowPolicy policy);

        /// @brief Start the consumer thread with an unbounded critical channel and a bounded debug channel.
        /// @param debug_capacity Maximum number of queued debug rows.
        /// @param policy Overflow policy when the debug queue is full.
        virtual void StartWithDebugChannel(size_t debug_capacity,
            QTrading::Utils::Queue::OverflowPolicy policy);

        /// @brief Stop the consumer thread, flush pending logs, and join.
        virtual void Stop();

        /// @brief Get the module id for a registered module name.
        /// @param module Module name.
        /// @return Module id, or kInvalidModuleId if not registered.
        ModuleId GetModuleId(const std::string& module) const;

        /// @brief Get a snapshot of logger metrics.
        MetricsSnapshot GetMetrics() const;

        /// @brief Send a log entry (non-blocking) by module id.
        /// @tparam T   Payload type.
        /// @param module_id Module id from RegisterModule.
        /// @param payload Intrusive payload pointer.
        /// @return true if enqueued, false if rejected or not started.
        inline bool Log(ModuleId module_id,
            PayloadPtr payload) noexcept
        {
            if (module_id == kInvalidModuleId) {
                enqueue_fail_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            ChannelKind kind = ChannelKind::Critical;
            if (module_id <= module_kinds_.size()) {
                kind = module_kinds_[module_id - 1];
            }
            Row r{
                module_id,
                QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed),
                std::move(payload)
            };
            auto target = channel;
            if (kind == ChannelKind::Debug && debug_channel_) {
                target = debug_channel_;
            }
            if (!target) {
                enqueue_fail_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            const bool ok = target->TrySend(std::move(r));
            if (ok) {
                enqueue_ok_.fetch_add(1, std::memory_order_relaxed);
                consume_cv_.notify_one();
            }
            else {
                enqueue_fail_.fetch_add(1, std::memory_order_relaxed);
            }
            return ok;
        }

        /// @brief Send a log entry by value (builds an intrusive payload).
        /// @tparam T   Payload type.
        /// @param module_id Module id from RegisterModule.
        /// @param obj   Payload object.
        /// @return true if enqueued, false if rejected or not started.
        template<typename T, typename std::enable_if_t<
            !std::is_same_v<std::decay_t<T>, PayloadPtr> &&
            !is_shared_ptr<std::decay_t<T>>::value, int> = 0>
        inline bool Log(ModuleId module_id,
            T&& obj) noexcept
        {
            return Log(module_id, MakePayload<std::decay_t<T>>(std::forward<T>(obj)));
        }

        /// @brief Send a log entry (non-blocking).
        /// @param module Module name.
        /// @param payload Payload pointer.
        /// @return true if enqueued, false if rejected or not started.
        inline bool Log(const std::string& module,
            PayloadPtr payload) noexcept
        {
            const auto module_id = GetModuleId(module);
            return Log(module_id, std::move(payload));
        }

        /// @brief Send a log entry by value (builds an intrusive payload).
        /// @tparam T   Payload type.
        /// @param module Module name.
        /// @param obj   Payload object.
        /// @return true if enqueued, false if rejected or not started.
        template<typename T, typename std::enable_if_t<
            !std::is_same_v<std::decay_t<T>, PayloadPtr> &&
            !is_shared_ptr<std::decay_t<T>>::value, int> = 0>
        inline bool Log(const std::string& module,
            T&& obj) noexcept
        {
            const auto module_id = GetModuleId(module);
            return Log(module_id, std::forward<T>(obj));
        }

        /// @brief Send multiple log entries (non-blocking), moving payloads from the array.
        /// @param module_id Module id from RegisterModule.
        /// @param payloads  Pointer to payload array (moved from).
        /// @param count     Number of payloads.
        /// @return Number of entries successfully enqueued.
        inline size_t LogBatch(ModuleId module_id,
            PayloadPtr* payloads,
            size_t count) noexcept
        {
            if (count == 0) {
                return 0;
            }
            if (module_id == kInvalidModuleId) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }
            ChannelKind kind = ChannelKind::Critical;
            if (module_id <= module_kinds_.size()) {
                kind = module_kinds_[module_id - 1];
            }
            auto target = channel;
            if (kind == ChannelKind::Debug && debug_channel_) {
                target = debug_channel_;
            }
            if (!target) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }

            size_t ok_count = 0;
            size_t fail_count = 0;
            for (size_t i = 0; i < count; ++i) {
                Row r{
                    module_id,
                    QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed),
                    std::move(payloads[i])
                };
                if (target->TrySend(std::move(r))) {
                    ++ok_count;
                }
                else {
                    ++fail_count;
                }
            }
            if (ok_count > 0) {
                enqueue_ok_.fetch_add(ok_count, std::memory_order_relaxed);
                consume_cv_.notify_one();
            }
            if (fail_count > 0) {
                enqueue_fail_.fetch_add(fail_count, std::memory_order_relaxed);
            }
            return ok_count;
        }

        /// @brief Send multiple log entries (non-blocking) with an explicit timestamp.
        /// @param module_id Module id from RegisterModule.
        /// @param payloads  Pointer to payload array (moved from).
        /// @param count     Number of entries.
        /// @param ts        Timestamp to apply to all entries.
        /// @return Number of entries successfully enqueued.
        inline size_t LogBatchAt(ModuleId module_id,
            PayloadPtr* payloads,
            size_t count,
            uint64_t ts) noexcept
        {
            if (count == 0) {
                return 0;
            }
            if (module_id == kInvalidModuleId) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }
            ChannelKind kind = ChannelKind::Critical;
            if (module_id <= module_kinds_.size()) {
                kind = module_kinds_[module_id - 1];
            }
            auto target = channel;
            if (kind == ChannelKind::Debug && debug_channel_) {
                target = debug_channel_;
            }
            if (!target) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }

            size_t ok_count = 0;
            size_t fail_count = 0;
            for (size_t i = 0; i < count; ++i) {
                Row r{
                    module_id,
                    ts,
                    std::move(payloads[i])
                };
                if (target->TrySend(std::move(r))) {
                    ++ok_count;
                }
                else {
                    ++fail_count;
                }
            }
            if (ok_count > 0) {
                enqueue_ok_.fetch_add(ok_count, std::memory_order_relaxed);
                consume_cv_.notify_one();
            }
            if (fail_count > 0) {
                enqueue_fail_.fetch_add(fail_count, std::memory_order_relaxed);
            }
            return ok_count;
        }

        /// @brief Send multiple log entries by value (builds intrusive payloads).
        /// @tparam T Payload type.
        /// @param module_id Module id from RegisterModule.
        /// @param objs   Pointer to payload objects.
        /// @param count  Number of objects.
        /// @return Number of entries successfully enqueued.
        template<typename T, typename std::enable_if_t<
            !std::is_same_v<std::decay_t<T>, PayloadPtr> &&
            !is_shared_ptr<std::decay_t<T>>::value, int> = 0>
        inline size_t LogBatch(ModuleId module_id,
            const T* objs,
            size_t count) noexcept
        {
            if (count == 0) {
                return 0;
            }
            if (module_id == kInvalidModuleId) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }
            ChannelKind kind = ChannelKind::Critical;
            if (module_id <= module_kinds_.size()) {
                kind = module_kinds_[module_id - 1];
            }
            auto target = channel;
            if (kind == ChannelKind::Debug && debug_channel_) {
                target = debug_channel_;
            }
            if (!target) {
                enqueue_fail_.fetch_add(count, std::memory_order_relaxed);
                return 0;
            }

            size_t ok_count = 0;
            size_t fail_count = 0;
            for (size_t i = 0; i < count; ++i) {
                Row r{
                    module_id,
                    QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed),
                    MakePayload<std::decay_t<T>>(objs[i])
                };
                if (target->TrySend(std::move(r))) {
                    ++ok_count;
                }
                else {
                    ++fail_count;
                }
            }
            if (ok_count > 0) {
                enqueue_ok_.fetch_add(ok_count, std::memory_order_relaxed);
                consume_cv_.notify_one();
            }
            if (fail_count > 0) {
                enqueue_fail_.fetch_add(fail_count, std::memory_order_relaxed);
            }
            return ok_count;
        }

        /// @brief Send multiple log entries (non-blocking), moving payloads from the array.
        /// @param module Module name.
        /// @param payloads Pointer to payload array (moved from).
        /// @param count Number of payloads.
        /// @return Number of entries successfully enqueued.
        inline size_t LogBatch(const std::string& module,
            PayloadPtr* payloads,
            size_t count) noexcept
        {
            const auto module_id = GetModuleId(module);
            return LogBatch(module_id, payloads, count);
        }

        /// @brief Send multiple log entries by value (builds intrusive payloads).
        /// @tparam T Payload type.
        /// @param module Module name.
        /// @param objs Pointer to payload objects.
        /// @param count Number of objects.
        /// @return Number of entries successfully enqueued.
        template<typename T, typename std::enable_if_t<
            !std::is_same_v<std::decay_t<T>, PayloadPtr> &&
            !is_shared_ptr<std::decay_t<T>>::value, int> = 0>
        inline size_t LogBatch(const std::string& module,
            const T* objs,
            size_t count) noexcept
        {
            const auto module_id = GetModuleId(module);
            return LogBatch(module_id, objs, count);
        }

    protected:
        /// @brief Register a module name and return its id.
        /// @param module Module name.
        /// @return Module id (stable for the module name).
        ModuleId RegisterModuleId(const std::string& module);

        /// @brief Register a module name and return its id with channel kind.
        /// @param module Module name.
        /// @param kind Channel kind for the module.
        /// @return Module id (stable for the module name).
        ModuleId RegisterModuleId(const std::string& module, ChannelKind kind);

        /// @brief Increment the flush counter.
        void IncrementFlushCount(uint64_t count = 1);

        /// @brief Wait for data and receive up to max_items without blocking channels.
        /// @param[out] out Batch of rows.
        /// @param max_items Maximum items to receive.
        /// @return false if all channels are closed and drained.
        bool WaitForBatch(std::vector<Row>& out, size_t max_items);

        std::string dir;  ///< Output directory for log files.

        /// @brief Called by the background thread to consume Rows.
        virtual void Consume() = 0;

        std::shared_ptr<QTrading::Utils::Queue::Channel<Row>> channel;      ///< Critical channel.
        std::shared_ptr<QTrading::Utils::Queue::Channel<Row>> debug_channel_; ///< Debug channel.
        boost::thread                                         consumer;     ///< Consumer thread.
        std::mutex                                            mtx;          ///< Protects start/stop.
        std::mutex                                            consume_mtx_; ///< Protects consume wait.
        std::condition_variable                               consume_cv_;  ///< Signals consumer when data arrives.

        std::unordered_map<std::string, ModuleId> module_ids_; ///< Module name to id.
        std::vector<ChannelKind> module_kinds_;                ///< Module id to channel kind.
        std::atomic<uint64_t> enqueue_ok_{ 0 };
        std::atomic<uint64_t> enqueue_fail_{ 0 };
        std::atomic<uint64_t> flush_count_{ 0 };
    };
}
