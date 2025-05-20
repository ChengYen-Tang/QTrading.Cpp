#pragma once

#include <memory>
#include <atomic>
#include <boost/thread.hpp>
#include <iostream>
#include <semaphore>
#include "Queue/Channel.hpp"

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
        }

        /// @brief Attach the exchange interface for order placement.
    /// @tparam TEx Type of the exchange, must satisfy IExchange<T>.
    /// @param ex Shared pointer to the exchange instance.
        template <typename TEx>
        void attach_exchange(std::shared_ptr<TEx> ex) {
            exchange = ex;
        }

        /// @brief Start the strategy's worker thread.
    /// @details Spawns a background thread running `run()`. Non-blocking call.
        void start() {
            if (worker.joinable()) return;
            std::cout << "[Strategy Module] Thread starting...\n";
            stop_flag.store(false);
            worker = boost::thread(&IStrategy::run, this);
        }

        /// @brief Stop the strategy's worker thread.
    /// @details Signals the thread to stop and joins it.
        void stop() {
            std::cout << "[Strategy Module] Thread stopping...\n";
            stop_flag.store(true);
            if (worker.joinable()) worker.join();
        }

        /// @brief Block until the current data point is fully processed.
        void wait_for_done() {
            sem.acquire();
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
        /// @brief Internal loop: read from `in`, call `on_data`, signal `sem`.
        void run() {
            while (!stop_flag.load()) {
                if (in->IsClosed() && !in->TryReceive()) break;
                auto v = in->Receive();
                if (v) on_data(v.value());
                sem.release();
            }
        }
    };
}
