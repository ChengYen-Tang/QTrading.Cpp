#include <gtest/gtest.h>
#include <thread>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

/// \brief Tests basic Send()/Receive() functionality when the channel is not full.
TEST(BoundedChannelTest, BasicSendReceive)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);

    EXPECT_TRUE(channel->Send(42));    ///< first element
    EXPECT_TRUE(channel->Send(100));   ///< second element

    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);       ///< check first received

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);      ///< check second received

    delete channel;
}

/// \brief Verify that when capacity is reached and policy=Reject, Send() returns false.
TEST(BoundedChannelTest, OverflowPolicyReject)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Reject);

    EXPECT_TRUE(channel->Send(1));     ///< fill the single slot
    EXPECT_FALSE(channel->Send(2));    ///< reject new element

    auto val = channel->Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    EXPECT_TRUE(channel->Send(3));     ///< now there is space again
    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 3);

    delete channel;
}

/// \brief Verify that when capacity is reached and policy=DropOldest, the oldest element is removed.
TEST(BoundedChannelTest, OverflowPolicyDropOldest)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(2, OverflowPolicy::DropOldest);

    EXPECT_TRUE(channel->Send(10));    ///< queue: [10]
    EXPECT_TRUE(channel->Send(20));    ///< queue: [10,20]

    EXPECT_TRUE(channel->Send(30));    ///< drop 10, queue becomes [20,30]

    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 20);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 30);

    delete channel;
}

/// \brief Test that Send() blocks when full (policy=Block) until space is available.
TEST(BoundedChannelTest, OverflowPolicyBlock)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Block);

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

    delete channel;
}

/// \brief Test that after Close(), existing items can be received but new Send() fails.
TEST(BoundedChannelTest, CloseBehavior)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(2);

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

    delete channel;
}

/// \brief Test non-blocking TryReceive() behavior.
TEST(BoundedChannelTest, TryReceive)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(2);

    auto empty1 = channel->TryReceive();
    EXPECT_FALSE(empty1.has_value()) << "TryReceive on empty should return nullopt";

    EXPECT_TRUE(channel->Send(123));
    auto val = channel->TryReceive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 123);

    auto empty2 = channel->TryReceive();
    EXPECT_FALSE(empty2.has_value());

    delete channel;
}

/// \brief Test that Receive() blocks when empty until Send() occurs.
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilSend)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);
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

    delete channel;
}

/// \brief Test that Receive() unblocks with nullopt on Close() when empty.
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilClose)
{
    Channel<int>* channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);
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

    delete channel;
}