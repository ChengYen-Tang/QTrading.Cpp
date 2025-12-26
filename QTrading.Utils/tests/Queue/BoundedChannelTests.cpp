#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <chrono>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

/// \brief Tests basic Send()/Receive() functionality when the channel is not full.
TEST(BoundedChannelTest, BasicSendReceive)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);

    EXPECT_TRUE(channel->Send(42));    ///< first element
    EXPECT_TRUE(channel->Send(100));   ///< second element

    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);       ///< check first received

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);      ///< check second received
}

/// \brief Verify that when capacity is reached and policy=Reject, Send() returns false.
TEST(BoundedChannelTest, OverflowPolicyReject)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Reject);

    EXPECT_TRUE(channel->Send(1));     ///< fill the single slot
    EXPECT_FALSE(channel->Send(2));    ///< reject new element

    auto val = channel->Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    EXPECT_TRUE(channel->Send(3));     ///< now there is space again
    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 3);
}

/// \brief Verify that when capacity is reached and policy=DropOldest, the oldest element is removed.
TEST(BoundedChannelTest, OverflowPolicyDropOldest)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(2, OverflowPolicy::DropOldest);

    EXPECT_TRUE(channel->Send(10));    ///< queue: [10]
    EXPECT_TRUE(channel->Send(20));    ///< queue: [10,20]

    EXPECT_TRUE(channel->Send(30));    ///< drop 10, queue becomes [20,30]

    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 20);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 30);
}

/// \brief Test that Send() blocks when full (policy=Block) until space is available.
TEST(BoundedChannelTest, OverflowPolicyBlock)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Block);

    EXPECT_TRUE(channel->Send(111));   ///< fill capacity

    // Launch a sender thread that should block until we Receive().
    std::thread sender([&]() {
        using Clock = std::chrono::steady_clock;
        auto start = Clock::now();
        bool result = channel->Send(222);    ///< must block until a slot frees
        auto end = Clock::now();
        auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        EXPECT_TRUE(result);
        EXPECT_GE(wait_ms, 50) << "Send() did not block long enough";
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  ///< ensure sender is waiting

    // Removing the only element should unblock the sender
    auto val = channel->Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 111);

    sender.join();

    // Now Receive the newly sent 222
    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 222);
}

/// \brief Test that after Close(), existing items can be received but new Send() fails.
TEST(BoundedChannelTest, CloseBehavior)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(2);

    EXPECT_TRUE(channel->Send(10));
    EXPECT_TRUE(channel->Send(20));

    channel->Close();                  ///< close the channel

    // Already queued items should still be readable
    auto v1 = channel->Receive();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(v1.value(), 10);

    auto v2 = channel->Receive();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2.value(), 20);

    // Now empty and closed => Receive() yields nullopt
    auto v3 = channel->Receive();
    EXPECT_FALSE(v3.has_value());
    EXPECT_TRUE(channel->IsClosed());

    // And Send() must fail on closed channel
    EXPECT_FALSE(channel->Send(30));
}

/// \brief Test non-blocking TryReceive() behavior.
TEST(BoundedChannelTest, TryReceive)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(2);

    auto empty1 = channel->TryReceive();
    EXPECT_FALSE(empty1.has_value()) << "TryReceive on empty should return nullopt";

    EXPECT_TRUE(channel->Send(123));
    auto val = channel->TryReceive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 123);

    auto empty2 = channel->TryReceive();
    EXPECT_FALSE(empty2.has_value());
}

/// \brief Test that Receive() blocks when empty until Send() occurs.
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilSend)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);
    std::optional<int> received;
    long long elapsed_ms = 0;

    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        received = channel->Receive();    ///< should block
        auto end = std::chrono::steady_clock::now();
        elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(channel->Send(999));     ///< unblock consumer

    consumer.join();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received.value(), 999);
    EXPECT_GE(elapsed_ms, 50);
}

/// \brief Test that Receive() unblocks with nullopt on Close() when empty.
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilClose)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);
    std::optional<int> received;
    long long elapsed_ms = 0;

    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        received = channel->Receive();    ///< should block until close
        auto end = std::chrono::steady_clock::now();
        elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    channel->Close();                    ///< cause Receive() to return nullopt

    consumer.join();
    EXPECT_FALSE(received.has_value());
    EXPECT_GE(elapsed_ms, 50);
}

/// \brief Multi-producer / single-consumer stress test. Verifies no loss and no duplicates.
TEST(BoundedChannelTest, MultiProducerSingleConsumer_NoLoss_NoDup)
{
    constexpr int producers = 4;
    constexpr int itemsPer = 5000;
    constexpr int total = producers * itemsPer;

    auto channel = ChannelFactory::CreateBoundedChannel<int>(1024, OverflowPolicy::Block);

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

TEST(BoundedChannelTest, ReceiveManyBasicAndUnblocksSend)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(2, OverflowPolicy::Block);

    EXPECT_TRUE(channel->Send(1));
    EXPECT_TRUE(channel->Send(2));

    std::atomic<bool> send_done{ false };
    std::thread sender([&]() {
        EXPECT_TRUE(channel->Send(3));
        send_done.store(true, std::memory_order_release);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(send_done.load(std::memory_order_acquire));

    auto batch = channel->ReceiveMany(2);
    ASSERT_EQ(batch.size(), 2u);
    EXPECT_EQ(batch[0], 1);
    EXPECT_EQ(batch[1], 2);

    sender.join();
    EXPECT_TRUE(send_done.load(std::memory_order_acquire));

    auto last = channel->Receive();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(*last, 3);

    auto empty = channel->ReceiveMany(10);
    EXPECT_TRUE(empty.empty());
}

TEST(BoundedChannelTest, DropOldestKeepsLastN_UnderHeavyOverflow)
{
    constexpr int capacity = 64;
    constexpr int totalSends = 1000;

    auto channel = ChannelFactory::CreateBoundedChannel<int>(capacity, OverflowPolicy::DropOldest);

    for (int i = 0; i < totalSends; ++i) {
        ASSERT_TRUE(channel->Send(i));
    }

    std::vector<int> got;
    got.reserve(capacity);
    for (;;) {
        auto v = channel->TryReceive();
        if (!v) break;
        got.push_back(*v);
    }

    ASSERT_EQ((int)got.size(), capacity);

    // Should contain the last `capacity` values in order.
    const int firstExpected = totalSends - capacity;
    for (int i = 0; i < capacity; ++i) {
        EXPECT_EQ(got[i], firstExpected + i);
    }
}

TEST(BoundedChannelTest, BlockPolicy_LongRun_NoDeadlock)
{
    auto channel = ChannelFactory::CreateBoundedChannel<int>(8, OverflowPolicy::Block);

    constexpr int producers = 4;
    constexpr int itemsPer = 5000;
    constexpr int total = producers * itemsPer;

    std::atomic<int> produced{ 0 };
    std::atomic<int> consumed{ 0 };

    std::vector<std::thread> prods;
    prods.reserve(producers);

    for (int p = 0; p < producers; ++p) {
        prods.emplace_back([&, p]() {
            for (int i = 0; i < itemsPer; ++i) {
                ASSERT_TRUE(channel->Send(p * itemsPer + i));
                produced.fetch_add(1, std::memory_order_relaxed);
            }
            });
    }

    std::thread consumer([&]() {
        while (consumed.load(std::memory_order_relaxed) < total) {
            auto v = channel->Receive();
            if (v) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        });

    for (auto& t : prods) t.join();
    EXPECT_EQ(produced.load(std::memory_order_relaxed), total);

    channel->Close();
    consumer.join();

    EXPECT_EQ(consumed.load(std::memory_order_relaxed), total);
}