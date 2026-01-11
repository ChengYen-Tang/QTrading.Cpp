#pragma once

#include <memory>
#include <atomic>
#include <boost/thread.hpp>
#include <iostream>
#include <semaphore>
#include <chrono>
#include "Queue/Channel.hpp"
#include "Debug/Trace.hpp"

namespace QTrading::Strategy {

    /// @brief Interface for all trading strategies.
    /// @tparam TIn The DTO type produced by a DataPreprocess module.
    template <typename TIn>
    class IStrategy {
    public:
        /// @brief Virtual destructor.
        virtual ~IStrategy() = default;

        /// @brief Attach an input channel for receiving DTOs.
        /// @param ch Shared pointer to a channel of shared_ptr<TIn>.
        void attach_in_channel(
            std::shared_ptr<QTrading::Utils::Queue::Channel<std::shared_ptr<TIn>>> ch)
        {
            in = std::move(ch);
            QTR_TRACE("strategy", "attach_in_channel");
        }

        /// @brief Attach the exchange interface for order placement.
        /// @tparam TEx Type of the exchange, must satisfy IExchange<T>.
        /// @param ex Shared pointer to the exchange instance.
        template <typename TEx>
        void attach_exchange(std::shared_ptr<TEx> ex) {
            exchange = ex;
            QTR_TRACE("strategy", "attach_exchange");
        }

        /// @brief Start the strategy's worker thread.
        /// @details Spawns a background thread running `run()`. Non-blocking call.
        void start() {
            if (worker.joinable()) return;
            std::cout << "[Strategy Module] Thread starting...\n";
            stop_flag.store(false);
            worker = boost::thread(&IStrategy::run, this);
            QTR_TRACE("strategy", "thread started");
        }

        /// @brief Stop the strategy's worker thread.
        /// @details Signals the thread to stop and joins it.
        void stop() {
            std::cout << "[Strategy Module] Thread stopping...\n";
            QTR_TRACE("strategy", "stop requested");
            stop_flag.store(true);
            if (worker.joinable()) worker.join();
            QTR_TRACE("strategy", "thread joined");
            sem.release();
        }

        /// @brief Block until the current data point is fully processed.
        void wait_for_done() {
            QTR_TRACE("strategy", "wait_for_done acquire begin");
            sem.acquire();
            QTR_TRACE("strategy", "wait_for_done acquire end");
        }

        /// @brief Block until the current data point is fully processed or timeout occurs.
        /// @param timeout Maximum time to wait.
        /// @return True if unblocked by data processing, false if timed out.
        bool wait_for_done_for(std::chrono::milliseconds timeout) {
            QTR_TRACE("strategy", "wait_for_done_for begin");
            const bool ok = sem.try_acquire_for(timeout);
            QTR_TRACE("strategy", ok ? "wait_for_done_for end ok" : "wait_for_done_for end timeout");
            return ok;
        }

    protected:
        std::shared_ptr<QTrading::Utils::Queue::Channel<std::shared_ptr<TIn>>> in; ///< Input channel.
        std::shared_ptr<void>                                                  exchange; ///< Exchange interface (cast in subclass).
        std::atomic<bool>                                                      stop_flag{ false }; ///< Stop signal.
        boost::thread                                                          worker; ///< Background thread.
        std::binary_semaphore                                                  sem{ 0 }; ///< Synchronization semaphore.

        /// @brief Called whenever a new DTO arrives.
        /// @param dto Shared pointer to the incoming data object.
        virtual void on_data(const std::shared_ptr<TIn>& dto) = 0;

    private:
        /// @brief Internal loop: read from `in`, call `on_data`, and always signal `sem` once per iteration.
        void run() {
            QTR_TRACE("strategy", "run loop begin");
            for (;;) {
                if (stop_flag.load()) {
                    QTR_TRACE("strategy", "run loop stop_flag break");
                    break;
                }

                QTR_TRACE("strategy", "Receive begin");
                auto v = in->Receive();
                QTR_TRACE("strategy", v ? "Receive got value" : "Receive nullopt (closed+empty?)");
                if (!v) break;

                QTR_TRACE("strategy", "on_data begin");
                on_data(*v);
                QTR_TRACE("strategy", "on_data end");

                sem.release();
                QTR_TRACE("strategy", "sem.release");
            }

            sem.release();
            QTR_TRACE("strategy", "run loop end (final sem.release)");
        }
    };
}
