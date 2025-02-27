#include <gtest/gtest.h>
#include <thread>
#include "Queue/ChannelFactory.hpp"

using namespace QTrading::Utils::Queue;

TEST(BoundedChannelTest, BasicSendReceive) {
	Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);

    EXPECT_TRUE(channel->Send(42));
    EXPECT_TRUE(channel->Send(100));

    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);

	delete channel;
}

// 2. OverflowPolicy::Reject 測試
// 當佇列已滿時，若 policy = Reject，Send() 應該返回 false 表示拒絕
TEST(BoundedChannelTest, OverflowPolicyReject) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Reject);

    EXPECT_TRUE(channel->Send(1));
    EXPECT_FALSE(channel->Send(2));

    auto val = channel->Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    EXPECT_TRUE(channel->Send(3));
    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 3);

	delete channel;
}

// 3. OverflowPolicy::DropOldest 測試
// 佇列已滿時，丟棄最舊的資料，保留較新的
TEST(BoundedChannelTest, OverflowPolicyDropOldest) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(2, OverflowPolicy::DropOldest);

    // 塞兩筆
    EXPECT_TRUE(channel->Send(10));
    EXPECT_TRUE(channel->Send(20));

    // 第三筆送進去時，要丟棄最舊的(10)，保留[20, 30]
    EXPECT_TRUE(channel->Send(30));

    // 取出資料，應該是 [20, 30]
    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 20);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 30);

	delete channel;
}

// 4. OverflowPolicy::Block 測試
// 佇列滿時，Sender 應被阻塞直到空間出現或被 Close
TEST(BoundedChannelTest, OverflowPolicyBlock) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(1, OverflowPolicy::Block);

    // 先放入一筆資料(現在佇列滿了)
    EXPECT_TRUE(channel->Send(111));

    // 在另一個 thread 裡 Send(222)，此時應該被阻塞
    // 在另一個 thread 裡做 Send(222)，此時預期要被阻塞
    std::thread sender([&channel]() {
        using Clock = std::chrono::steady_clock;
        auto start = Clock::now();

        // 只有在有人 Receive() 後才會繼續往下 (解阻塞)
        bool result = channel->Send(222);

        auto end = Clock::now();
        auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        EXPECT_TRUE(result);
        // 檢查是否至少阻塞了 50ms (或其他你覺得合理的值)
        // 如果要放寬，可以改大一點免得測試在繁忙環境時失敗
        EXPECT_GE(diff_ms, 50) << "Send should have been blocked for some time.";
        });

    // 稍微等一下，確保 sender 執行到了 Send()
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 檢查佇列目前只有 111
    // 若 Block 正常，則 sender 應該還在等待
    auto val = channel->Receive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 111);

    // 一旦取走 111，sender thread 那邊就可以繼續放 222
    sender.join();

    // 現在再把 222 收掉
    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 222);

	delete channel;
}

// 5. 測試 Close 之後行為
TEST(BoundedChannelTest, CloseBehavior) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(2);

    // 先放兩筆
    EXPECT_TRUE(channel->Send(10));
    EXPECT_TRUE(channel->Send(20));

    // 關閉
    channel->Close();

    // 關閉後可以繼續拿現有資料
    auto val1 = channel->Receive();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 10);

    auto val2 = channel->Receive();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 20);

    // 拿完後，如果佇列空了，就回傳 nullopt
    auto val3 = channel->Receive();
    EXPECT_FALSE(val3.has_value());
	EXPECT_TRUE(channel->IsClosed());

    // 關閉後 Send() 應該直接回傳 false
    EXPECT_FALSE(channel->Send(30));

	delete channel;
}

// 6. TryReceive 測試（非阻塞）
TEST(BoundedChannelTest, TryReceive) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(2);

    // 最初佇列空, TryReceive 應該為空
    auto emptyVal = channel->TryReceive();
    EXPECT_FALSE(emptyVal.has_value());

    // 放一筆資料
    EXPECT_TRUE(channel->Send(123));

    // TryReceive 立即可拿到 123
    auto val = channel->TryReceive();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 123);

    // 再一次應該拿不到資料(空了)
    auto emptyVal2 = channel->TryReceive();
    EXPECT_FALSE(emptyVal2.has_value());

	delete channel;
}

// 測試：在空佇列時，Receive() 會阻塞，直到有人 Send()。
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilSend) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);

    // 我們想量測 consumer 在 Receive() 卡了多久
    std::optional<int> receivedValue;
    long long blockDurationMs = 0; // 紀錄測到的阻塞時間（毫秒）

    std::thread consumer([&]() {
        // 記錄開始時間
        auto start = std::chrono::steady_clock::now();

        // 這裡佇列還是空，所以會被阻塞
        receivedValue = channel->Receive();

        // 記錄結束時間
        auto end = std::chrono::steady_clock::now();
        blockDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    // 稍微等一下，讓 consumer 執行緒確定在等待
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 主執行緒送資料 -> 消費者就能解除阻塞
    EXPECT_TRUE(channel->Send(999));

    // 等待 consumer join
    consumer.join();

    // 最終結果檢查
    ASSERT_TRUE(receivedValue.has_value());
    EXPECT_EQ(receivedValue.value(), 999);

    // 檢查是否阻塞了至少 50ms (值可以自行斟酌)
    EXPECT_GE(blockDurationMs, 50)
        << "Consumer did not appear to block as long as expected.";

	delete channel;
}


// 測試：在空佇列時，如果沒有資料送進來，且之後呼叫 Close()，Receive() 應該回傳 nullopt
TEST(BoundedChannelTest, ReceiveBlocksWhenEmptyUntilClose_TimeCheck) {
    Channel<int> *channel = ChannelFactory::CreateBoundedChannel<int>(5, OverflowPolicy::Block);

    std::optional<int> receivedValue;
    long long blockDurationMs = 0;

    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        receivedValue = channel->Receive();
        auto end = std::chrono::steady_clock::now();
        blockDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });

    // 先等 consumer 進入 Receive() 並被阻塞
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 現在關閉 channel
    channel->Close();

    // 等 consumer 執行緒結束
    consumer.join();

    // 因為關閉且佇列空，所以理應回傳 nullopt
    EXPECT_FALSE(receivedValue.has_value());

    // 如果預期它至少被阻塞了 50ms，可以這樣檢查
    EXPECT_GE(blockDurationMs, 50);

	delete channel;
}
