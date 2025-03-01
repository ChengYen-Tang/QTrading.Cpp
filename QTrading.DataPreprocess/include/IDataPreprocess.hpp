#pragma once

#include <atomic>
#include <iostream>
#include <Memory>
#include <boost/thread.hpp>
#include "Queue/Channel.hpp"

using namespace std;
using namespace QTrading::Utils::Queue;

namespace QTrading::DataPreprocess {
	template <typename T>
	class IDataPreprocess {
	public:
		shared_ptr<Channel<T>> get_market_channel() const {
			return market_channel;
		}

        void start() {
            if (workerThread) {
                std::cout << "[Data Preprocess Module] Thread is already running!\n";
                return;
            }

            std::cout << "[Data Preprocess Module] Starting thread...\n";
            workerThread = boost::thread(&IDataPreprocess::run, this);
        }

        void stop() {
            std::cout << "[Data PreprocessModule] Stopping thread...\n";
            stopFlag.store(true);
            if (workerThread.joinable()) {
                workerThread.join();
            }
        }

		virtual ~IDataPreprocess() {
			stop();
			market_channel->Close();
		}
	protected:
		std::atomic<bool> stopFlag;
		shared_ptr<Channel<T>> market_channel;
		boost::thread workerThread;

		virtual void run() = 0;
	};
}
