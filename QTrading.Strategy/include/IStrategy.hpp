#pragma once
/**
 * Generic strategy interface
 *
 *  TIn  – the DTO that comes out of IDataPreprocess
 */
#include <memory>
#include <atomic>
#include <boost/thread.hpp>
#include <semaphore>
#include "Queue/Channel.hpp"

namespace QTrading::Strategy {

    template <typename TIn>
    class IStrategy {
    public:
        virtual ~IStrategy() = default;

        /** wire channels (done by Strategy‑runner / main()) */
        void attach_in_channel(std::shared_ptr<QTrading::Utils::Queue::Channel<std::shared_ptr<TIn>>> ch)
        {
            in = std::move(ch);
        }

        template <typename TEx>
        void attach_exchange(std::shared_ptr<TEx> ex) { exchange = ex; }

        /** spawn worker thread (non‑blocking) */
        void start() {
            if (worker.joinable()) return;
            stop_flag.store(false);
            worker = boost::thread(&IStrategy::run, this);
        }
        void stop() {
            std::cout << "[Strategy Module] Stopping thread...\n";
            stop_flag.store(true);
            if (worker.joinable()) worker.join();
        }
        void wait_for_done() {
            sem.acquire();
        }
    protected:
        std::shared_ptr<QTrading::Utils::Queue::Channel<std::shared_ptr<TIn>>>  in;
        std::shared_ptr<void>                                                   exchange;   // cast in subclass
        std::atomic<bool>                                                       stop_flag { false };
        boost::thread                                                           worker;
        std::binary_semaphore                                                   sem{ 0 };

        /** strategy must implement this – will be called each time a dto arrives */
        virtual void on_data(const std::shared_ptr<TIn>& dto) = 0;

    private:
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
