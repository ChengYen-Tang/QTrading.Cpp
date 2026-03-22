#pragma once

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Output/LegacyEventEnvelopeEmitter.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Output {

class BinanceExchange::IEventPublisher {
public:
    virtual ~IEventPublisher() = default;
    virtual void publish(EventEnvelope&& task) = 0;
};

class BinanceExchange::NullEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    void publish(EventEnvelope&&) override {}
};

class BinanceExchange::LegacyEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    explicit LegacyEventPublisher(BinanceExchange& owner)
        : owner_(owner)
    {
    }

    void publish(EventEnvelope&& task) override
    {
        Output::LegacyEventEnvelopeEmitter(owner_).emit(std::move(task));
    }

private:
    BinanceExchange& owner_;
};

class BinanceExchange::AsyncEventPublisher final : public BinanceExchange::IEventPublisher {
public:
    explicit AsyncEventPublisher(std::unique_ptr<BinanceExchange::IEventPublisher> downstream)
        : downstream_(std::move(downstream))
    {
        worker_thread_ = std::thread([this]() { worker_(); });
    }

    ~AsyncEventPublisher() override
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void publish(EventEnvelope&& task) override
    {
        if (!downstream_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.emplace_back(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_()
    {
        while (true) {
            EventEnvelope task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]() {
                    return stop_.load(std::memory_order_acquire) || !queue_.empty();
                    });
                if (stop_.load(std::memory_order_acquire) && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            downstream_->publish(std::move(task));
        }
    }

    std::unique_ptr<BinanceExchange::IEventPublisher> downstream_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<EventEnvelope> queue_;
    std::thread worker_thread_;
    std::atomic<bool> stop_{ false };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Output
