#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "Queue/BoundedChannel.hpp"

TEST(BoundedChannelTest, BasicSendReceive) {
    BoundedChannel<int> channel(5, OverflowPolicy::Block);

    EXPECT_TRUE(channel.Send(42));
    EXPECT_TRUE(channel.Send(100));

    auto val1 = channel.Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);

    auto val2 = channel.Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);
}


TEST(BoundedChannelTest, OverflowPolicyReject) {
    BoundedChannel<int> channel(1, OverflowPolicy::Reject);

    EXPECT_TRUE(channel.Send(1));

    EXPECT_FALSE(channel.Send(2));

    auto val = channel.Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    EXPECT_TRUE(channel.Send(3));
}

TEST(BoundedChannelTest, OverflowPolicyDropOldest) {
    BoundedChannel<int> channel(2, OverflowPolicy::DropOldest);

    EXPECT_TRUE(channel.Send(10));
    EXPECT_TRUE(channel.Send(20));

    EXPECT_TRUE(channel.Send(30));

    auto val1 = channel.Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 20);

    auto val2 = channel.Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 30);
}

TEST(BoundedChannelTest, OverflowPolicyBlock) {
    BoundedChannel<int> channel(1, OverflowPolicy::Block);

    EXPECT_TRUE(channel.Send(111));

    std::thread sender([&channel]() {
        channel.Send(222);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto val = channel.Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 111);

    sender.join();

    auto val2 = channel.Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 222);
}

TEST(BoundedChannelTest, CloseBehavior) {
    BoundedChannel<int> channel(2);

    EXPECT_TRUE(channel.Send(10));
    EXPECT_TRUE(channel.Send(20));

    channel.Close();

    auto val1 = channel.Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 10);

    auto val2 = channel.Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 20);

    auto val3 = channel.Receive();
    EXPECT_FALSE(val3.has_value());

    EXPECT_FALSE(channel.Send(30));
}

TEST(BoundedChannelTest, TryReceive) {
    BoundedChannel<int> channel(2);

    auto emptyVal = channel.TryReceive();
    EXPECT_FALSE(emptyVal.has_value());

    channel.Send(123);

    auto val = channel.TryReceive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 123);

    auto emptyVal2 = channel.TryReceive();
    EXPECT_FALSE(emptyVal2.has_value());
}
