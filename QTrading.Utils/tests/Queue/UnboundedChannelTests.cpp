#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <chrono>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

/// \brief Basic Send()/Receive() under normal conditions (unbounded capacity).
TEST(UnboundedChannelTest, BasicSendReceive)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    EXPECT_TRUE(channel->Send(42));
    EXPECT_TRUE(channel->Send(100));

    auto v1 = channel->Receive();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(v1.value(), 42);

    auto v2 = channel->Receive();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2.value(), 100);
}

/// \brief TryReceive returns nullopt when empty, then returns value when data is available.
TEST(UnboundedChannelTest, TryReceive)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    auto empty1 = channel->TryReceive();
    EXPECT_FALSE(empty1.has_value());

    EXPECT_TRUE(channel->Send(999));
    auto v = channel->TryReceive();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v.value(), 999);

    auto empty2 = channel->TryReceive();
    EXPECT_FALSE(empty2.has_value());
}

/// \brief After Close(), existing items are still readable; Send() fails; Receive() on empty returns nullopt.
TEST(UnboundedChannelTest, CloseBehavior)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    EXPECT_TRUE(channel->Send(1));
    EXPECT_TRUE(channel->Send(2));

    channel->Close();

    EXPECT_FALSE(channel->Send(3));    ///< no new sends allowed

    auto r1 = channel->Receive();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1.value(), 1);

    auto r2 = channel->Receive();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 2);

    auto r3 = channel->Receive();
    EXPECT_FALSE(r3.has_value());

    auto tr = channel->TryReceive();
    EXPECT_FALSE(tr.has_value());
}

/// \brief Receive() blocks when empty until a Send() arrives.
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilSend)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();
    std::optional<int> received;
    long long wait_ms = 0;

    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        received = channel->Receive();
        auto end = std::chrono::steady_clock::now();
        wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(channel->Send(123));

    consumer.join();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received.value(), 123);
    EXPECT_GE(wait_ms, 50);
}

/// \brief Receive() unblocks with nullopt on Close() when empty.
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilClose)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();
    std::optional<int> received;
    long long wait_ms = 0;

    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        received = channel->Receive();
        auto end = std::chrono::steady_clock::now();
        wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    channel->Close();

    consumer.join();
    EXPECT_FALSE(received.has_value());
    EXPECT_GE(wait_ms, 50);
}

/// \brief Simple multi-producer / single-consumer test.
TEST(UnboundedChannelTest, MultiThreadSendReceive)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    constexpr int producers = 3;
    constexpr int itemsPer = 5;
    std::vector<std::thread> threads;
    std::vector<int> received;

    // Producers
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p]() {
            for (int i = 0; i < itemsPer; ++i) {
                channel->Send(p * 1000 + i);
            }
            });
    }

    // Consumer
    std::thread consumer([&]() {
        int total = producers * itemsPer;
        while ((int)received.size() < total) {
            auto v = channel->Receive();
            if (v) received.push_back(*v);
        }
        });

    for (auto& t : threads) t.join();
    channel->Close();
    consumer.join();

    EXPECT_EQ(received.size(), producers * itemsPer);
}

/// \brief Multi-producer / single-consumer stress test. Verifies no loss and no duplicates.
TEST(UnboundedChannelTest, MultiProducerSingleConsumer_NoLoss_NoDup)
{
    constexpr int producers = 4;
    constexpr int itemsPer = 5000;
    constexpr int total = producers * itemsPer;

    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    std::atomic<int> produced{ 0 };

    std::vector<std::thread> prodThreads;
    prodThreads.reserve(producers);

    for (int p = 0; p < producers; ++p) {
        prodThreads.emplace_back([&, p]() {
            for (int i = 0; i < itemsPer; ++i) {
                int v = p * itemsPer + i;
                ASSERT_TRUE(channel->Send(v));
                produced.fetch_add(1, std::memory_order_relaxed);
            }
            });
    }

    std::vector<int> received;
    received.reserve(total);

    std::thread consumer([&]() {
        while ((int)received.size() < total) {
            auto v = channel->Receive();
            if (v) {
                received.push_back(*v);
            }
        }
        });

    for (auto& t : prodThreads) t.join();
    EXPECT_EQ(produced.load(std::memory_order_relaxed), total);

    channel->Close();
    consumer.join();

    ASSERT_EQ((int)received.size(), total);

    std::unordered_set<int> uniq;
    uniq.reserve(received.size());
    for (int v : received) {
        ASSERT_TRUE(uniq.insert(v).second) << "Duplicate value received: " << v;
    }
}

TEST(UnboundedChannelTest, ReceiveManyBasic)
{
    auto channel = ChannelFactory::CreateUnboundedChannel<int>();

    EXPECT_TRUE(channel->Send(1));
    EXPECT_TRUE(channel->Send(2));
    EXPECT_TRUE(channel->Send(3));

    auto batch = channel->ReceiveMany(2);
    ASSERT_EQ(batch.size(), 2u);
    EXPECT_EQ(batch[0], 1);
    EXPECT_EQ(batch[1], 2);

    auto batch2 = channel->ReceiveMany(10);
    ASSERT_EQ(batch2.size(), 1u);
    EXPECT_EQ(batch2[0], 3);

    auto empty = channel->ReceiveMany(10);
    EXPECT_TRUE(empty.empty());
}

/// \brief Larger MPSC stress test to exercise ChunkedQueue block rollover/release paths.
TEST(UnboundedChannelTest, MultiProducerSingleConsumer_Larger_NoLoss_NoDup)
{
    constexpr int producers = 8;
    constexpr int itemsPer = 20000;
    constexpr int total = producers * itemsPer;

    // Use a small block capacity to force frequent block creation.
    auto channel = ChannelFactory::CreateUnboundedChannel<int>(128);

    std::atomic<int> produced{ 0 };

    std::vector<std::thread> prodThreads;
    prodThreads.reserve(producers);

    for (int p = 0; p < producers; ++p) {
        prodThreads.emplace_back([&, p]() {
            for (int i = 0; i < itemsPer; ++i) {
                int v = p * itemsPer + i;
                ASSERT_TRUE(channel->Send(v));
                produced.fetch_add(1, std::memory_order_relaxed);
            }
            });
    }

    std::vector<int> received;
    received.reserve(total);

    std::thread consumer([&]() {
        while ((int)received.size() < total) {
            auto v = channel->Receive();
            if (v) received.push_back(*v);
        }
        });

    for (auto& t : prodThreads) t.join();
    EXPECT_EQ(produced.load(std::memory_order_relaxed), total);

    channel->Close();
    consumer.join();

    ASSERT_EQ((int)received.size(), total);

    std::unordered_set<int> uniq;
    uniq.reserve(received.size());
    for (int v : received) {
        ASSERT_TRUE(uniq.insert(v).second) << "Duplicate value received: " << v;
    }
}

/// \brief Verifies that Close() drains remaining items correctly even with large backlog.
TEST(UnboundedChannelTest, CloseDrainsLargeBacklog)
{
    constexpr int total = 50000;
    auto channel = ChannelFactory::CreateUnboundedChannel<int>(256);

    for (int i = 0; i < total; ++i) {
        ASSERT_TRUE(channel->Send(i));
    }

    channel->Close();

    int count = 0;
    for (;;) {
        auto v = channel->Receive();
        if (!v) break;
        EXPECT_EQ(*v, count);
        ++count;
    }

    EXPECT_EQ(count, total);
}