#include <gtest/gtest.h>
#include <thread>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

// 1. 測試基本 Send / Receive
TEST(UnboundedChannelTest, BasicSendReceive) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    // 測試 Send()
    EXPECT_TRUE(channel->Send(42));
    EXPECT_TRUE(channel->Send(100));

    // 測試 Receive()
    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);

	delete channel;
}

// 2. 測試 TryReceive (在有資料的情況 / 沒資料的情況)
TEST(UnboundedChannelTest, TryReceive) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    // 起初沒資料，TryReceive() 應該回傳 nullopt
    auto emptyVal = channel->TryReceive();
    EXPECT_FALSE(emptyVal.has_value());

    // Send 資料
    EXPECT_TRUE(channel->Send(999));
    // 立刻 TryReceive()，應該拿到 999
    auto val = channel->TryReceive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 999);

    // 之後再拿應該拿不到資料
    auto emptyVal2 = channel->TryReceive();
    EXPECT_FALSE(emptyVal2.has_value());

	delete channel;
}

// 3. 測試 Close() 行為
TEST(UnboundedChannelTest, CloseBehavior) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    // 先送一些資料
    EXPECT_TRUE(channel->Send(1));
    EXPECT_TRUE(channel->Send(2));

    // 關閉
    channel->Close();

    // 關閉後應該無法再送
    EXPECT_FALSE(channel->Send(3));

    // 仍可繼續拿到先前已送的資料
    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 1);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 2);

    // 已取完，佇列現在是空
    // 因為 channel 已關閉，再呼叫 Receive() 應該回傳 nullopt
    auto val3 = channel->Receive();
    EXPECT_FALSE(val3.has_value());

    // TryReceive() 也應該是空
    auto val4 = channel->TryReceive();
    EXPECT_FALSE(val4.has_value());

    delete channel;
}

// 4. 測試：佇列為空時，Receive() 會阻塞直到有人 Send()
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilSend) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    // consumerThread 在空佇列時呼叫 Receive()，理應被阻塞
    std::optional<int> receivedVal;
    std::thread consumerThread([&]() {
        auto start = std::chrono::steady_clock::now();

        // 這行會阻塞，直到有人 Send 或 channel 被 Close
        receivedVal = channel->Receive();

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        EXPECT_GE(elapsed, 50) << "Receive() did not block long enough!";
        });

    // 在這裡先等 500ms，再送資料 -> 讓 consumerThread 解阻塞
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    channel->Send(123);

    consumerThread.join();

    // 驗證確實拿到資料
    ASSERT_TRUE(receivedVal.has_value());
    EXPECT_EQ(receivedVal.value(), 123);

    delete channel;
}

// 4-1. 如果呼叫 Close()，佇列空時 Receive() 應該回傳 nullopt 並解除阻塞
TEST(UnboundedChannelTest, ReceiveBlocksWhenEmptyUntilClose) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    std::optional<int> receivedVal;
    std::thread consumerThread([&]() {
        auto start = std::chrono::steady_clock::now();

        // 會阻塞，直到佇列不空 or close
        receivedVal = channel->Receive();

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        EXPECT_GE(elapsed, 50) << "Receive() did not block at least 50ms before Close() woke it!";
        });

    // 主執行緒等 500ms，然後 Close
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    channel->Close();

    consumerThread.join();

    // 因為 channel 被關，且之前沒送任何資料，所以結果應該是 nullopt
    EXPECT_FALSE(receivedVal.has_value());

    delete channel;
}

// 5. 測試多執行緒 Send/Receive (簡易測試)
TEST(UnboundedChannelTest, MultiThreadSendReceive) {
    Channel<int> *channel = ChannelFactory::CreateUnboundedChannel<int>();

    constexpr int totalProducers = 3;
    constexpr int totalItemsPerProducer = 5;

    auto producerFunc = [&](int startVal) {
        for (int i = 0; i < totalItemsPerProducer; ++i) {
            // 每個生產者放入 startVal+i
            channel->Send(startVal + i);
        }
        };

    // 建立多個生產者執行緒
    std::vector<std::thread> producers;
    for (int p = 0; p < totalProducers; ++p) {
        producers.emplace_back(producerFunc, p * 1000);
    }

    // 消費者執行緒來拿資料
    // 預期總共會有 totalProducers * totalItemsPerProducer 筆資料
    std::vector<int> receivedValues;
    std::thread consumer([&]() {
        int count = 0;
        while (count < totalProducers * totalItemsPerProducer) {
            auto val = channel->Receive();
            if (val.has_value()) {
                receivedValues.push_back(val.value());
                count++;
            }
        }
        });

    // 等所有生產者結束
    for (auto& thr : producers) {
        thr.join();
    }
    // producers 都送完之後，再關閉 channel (選擇性)
    channel->Close();

    // 等消費者結束
    consumer.join();

    // 驗證共收到了 totalProducers * totalItemsPerProducer 筆資料
    EXPECT_EQ(receivedValues.size(), totalProducers * totalItemsPerProducer);

    delete channel;
}