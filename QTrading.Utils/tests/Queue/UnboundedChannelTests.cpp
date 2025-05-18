#include <gtest/gtest.h>
#include <thread>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

/// \brief Basic Send()/Receive() under normal conditions (unbounded capacity).
TEST(UnboundedChannelTest, BasicSendReceive)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();

    EXPECT_TRUE(channel->Send(42));
    EXPECT_TRUE(channel->Send(100));

    auto v1 = channel->Receive();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(v1.value(), 42);

    auto v2 = channel->Receive();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2.value(), 100);

    delete channel;
}

/// \brief TryReceive returns nullopt when empty, then returns value when data is available.
TEST(UnboundedChannelTest, TryReceive)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();

    auto empty1 = channel->TryReceive();
    EXPECT_FALSE(empty1.has_value());

    EXPECT_TRUE(channel->Send(999));
    auto v = channel->TryReceive();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v.value(), 999);

    auto empty2 = channel->TryReceive();
    EXPECT_FALSE(empty2.has_value());

    delete channel;
}

/// \brief After Close(), existing items are still readable; Send() fails; Receive() on empty returns nullopt.
TEST(UnboundedChannelTest, CloseBehavior)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();

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

    delete channel;
}

/// \brief Receive() blocks when empty until a Send() arrives.
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilSend)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();
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

    delete channel;
}

/// \brief Receive() unblocks with nullopt on Close() when empty.
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilClose)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();
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

    delete channel;
}

/// \brief Simple multi-producer / single-consumer test.
TEST(UnboundedChannelTest, MultiThreadSendReceive)
{
    Channel<int>* channel = ChannelFactory::CreateUnboundedChannel<int>();

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

    delete channel;
}