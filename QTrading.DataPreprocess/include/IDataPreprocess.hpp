#pragma once

#include <atomic>
#include <iostream>
#include <memory>
#include <boost/thread.hpp>
#include "Queue/Channel.hpp"

using namespace std;
using namespace QTrading::Utils::Queue;

namespace QTrading::DataPreprocess {

    /// @brief  Abstract base for data preprocessing modules.
    /// @tparam T  Type of DTO that this module produces on its market channel.
    template <typename T>
    class IDataPreprocess {
    public:
        /// @brief  Get the downstream market channel for processed data.
        /// @return Shared pointer to the output Channel of type T.
        shared_ptr<Channel<T>> get_market_channel() const {
            return market_channel;
        }

        /// @brief  Start the preprocessing thread.
        /// @throws runtime_error if the module has already been stopped.
        void start() {
            if (stopFlag.load()) {
                std::cout << "[Data Preprocess Module] Thread has been stopped!\n";
                std::cout << "[Data Preprocess Module] Please create a new instance to start again!\n";
                throw std::runtime_error("The module has expired.");
            }
            if (workerThread.joinable()) {
                std::cout << "[Data Preprocess Module] Thread is already running!\n";
                return;
            }

            std::cout << "[Data Preprocess Module] Starting thread...\n";
            workerThread = boost::thread(&IDataPreprocess::run, this);
        }

        /// @brief  Stop the preprocessing thread and close output channel.
        void stop() {
            std::cout << "[Data PreprocessModule] Stopping thread...\n";
            stopFlag.store(true);
            if (workerThread.joinable()) {
                workerThread.join();
            }
            market_channel->Close();
        }

        /// @brief  Virtual destructor stops the thread if still running.
        virtual ~IDataPreprocess() {
            stop();
        }

    protected:
        std::atomic<bool>                     stopFlag;        ///< Flag to signal thread termination
        shared_ptr<Channel<T>>               market_channel;  ///< Output channel for processed data
        boost::thread                        workerThread;    ///< Worker thread

        /// @brief  Main loop; must be implemented by subclasses.
        virtual void run() = 0;
    };
}
