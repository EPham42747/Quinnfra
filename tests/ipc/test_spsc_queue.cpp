#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <quinnfra/core/alignment.hpp>
#include <quinnfra/ipc/spsc_consumer.hpp>
#include <quinnfra/ipc/spsc_producer.hpp>

namespace quinnfra::ipc::testing {

namespace {

struct alignas(64) TestMessage {
    uint64_t timestamp_ns{0};
    uint32_t seq{0};
    uint8_t  padding[52]{0};
};

static_assert(sizeof(TestMessage) == 64, "TestMessage must fit 1 cache line");
static_assert(std::is_trivially_copyable_v<TestMessage>, "TestMessage must be trivially copyable");

TestMessage make_message(uint32_t seq) {
    TestMessage msg{};
    msg.timestamp_ns = 1'000'000ULL * seq;
    msg.seq = seq;
    return msg;
}

} // namespace

// 1. Memory Layout and Cache Line False-Sharing Checks
TEST(IpcSpscQueueTest, MemoryLayoutAndCacheAlignment) {
    using TestQueue = detail::RingBufferLayout<TestMessage, 1024>;

    // Ensure the struct and its critical members meet the 64-byte alignment requirement
    EXPECT_EQ(alignof(TestQueue), quinnfra::core::hardware_destructive_interference_size);
    EXPECT_EQ(offsetof(TestQueue, write_index) % quinnfra::core::hardware_destructive_interference_size, 0);
    EXPECT_EQ(offsetof(TestQueue, read_index) % quinnfra::core::hardware_destructive_interference_size, 0);
    EXPECT_EQ(offsetof(TestQueue, buffer) % quinnfra::core::hardware_destructive_interference_size, 0);

    // Ensure write_index and read_index reside on strictly separate cache lines
    size_t index_distance = offsetof(TestQueue, read_index) - offsetof(TestQueue, write_index);
    EXPECT_GE(index_distance, quinnfra::core::hardware_destructive_interference_size);
}

// 2. Empty Queue Behavior
TEST(IpcSpscQueueTest, EmptyQueueReturnsNullptr) {
    constexpr size_t Capacity = 16;
    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    SpscConsumer<TestMessage, Capacity> consumer(layout.get());

    EXPECT_EQ(consumer.front(), nullptr);
}

// 3. FIFO Ordering and Peek/Pop Verification
TEST(IpcSpscQueueTest, FifoOrderingAndPeekPop) {
    constexpr size_t Capacity = 8;
    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    SpscProducer<TestMessage, Capacity> producer(layout.get());
    SpscConsumer<TestMessage, Capacity> consumer(layout.get());

    EXPECT_TRUE(producer.try_push(make_message(1)));
    EXPECT_TRUE(producer.try_push(make_message(2)));

    // Peek first event: front() must not advance the read pointer
    const TestMessage* msg1 = consumer.front();
    ASSERT_NE(msg1, nullptr);
    EXPECT_EQ(msg1->seq, 1);

    EXPECT_EQ(consumer.front(), msg1);

    // Pop first event
    consumer.pop();

    // Now front() should point to the second event
    const TestMessage* msg2 = consumer.front();
    ASSERT_NE(msg2, nullptr);
    EXPECT_EQ(msg2->seq, 2);

    consumer.pop();
    EXPECT_EQ(consumer.front(), nullptr);
}

// 4. Capacity Bound and Drop Tracking
TEST(IpcSpscQueueTest, CapacityAndDropTracking) {
    constexpr size_t Capacity = 4;
    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    SpscProducer<TestMessage, Capacity> producer(layout.get());
    SpscConsumer<TestMessage, Capacity> consumer(layout.get());

    // Fill buffer to capacity
    for (size_t i = 0; i < Capacity; ++i) {
        EXPECT_TRUE(producer.try_push(make_message(static_cast<uint32_t>(i))));
    }

    // Next push MUST fail cleanly without overwriting or hanging
    EXPECT_FALSE(producer.try_push(make_message(99)));
    EXPECT_EQ(consumer.dropped_count(), 1);

    // Drain one slot
    ASSERT_NE(consumer.front(), nullptr);
    EXPECT_EQ(consumer.front()->seq, 0);
    consumer.pop();
    
    // Now pushing should succeed once
    EXPECT_TRUE(producer.try_push(make_message(100)));
    EXPECT_FALSE(producer.try_push(make_message(101)));
}

// 5. Index Wrap-Around
TEST(IpcSpscQueueTest, ContinuousWrapAroundIntegrity) {
    constexpr size_t Capacity = 8;
    constexpr size_t TotalEvents = 100'000;

    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    SpscProducer<TestMessage, Capacity> producer(layout.get());
    SpscConsumer<TestMessage, Capacity> consumer(layout.get());

    for (size_t i = 0; i < TotalEvents; ++i) {
        ASSERT_TRUE(producer.try_push(make_message(static_cast<uint32_t>(i))));

        const TestMessage* item = consumer.front();
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->seq, static_cast<uint32_t>(i));
        consumer.pop();
    }

    EXPECT_EQ(consumer.front(), nullptr);
    EXPECT_EQ(layout->write_index.load(), TotalEvents);
    EXPECT_EQ(layout->read_index.load(), TotalEvents);
}

// 6. Concurrent Multi-Threaded Stress Test
TEST(IpcSpscQueueTest, ConcurrentStreamingNoLossOrCorruption) {
    constexpr size_t Capacity = 1024;
    constexpr size_t MessageCount = 2'000'000;

    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    std::atomic<bool> producer_done{false};

    std::vector<uint32_t> received_sequences;
    received_sequences.reserve(MessageCount);

    // Consumer thread: reads until producer is done and queue is drained
    std::thread consumer_thread([&]() {
        SpscConsumer<TestMessage, Capacity> consumer(layout.get());

        while (!producer_done.load(std::memory_order_relaxed) || consumer.front() != nullptr) {
            if (const auto* msg = consumer.front()) {
                // Verify payload integrity against sequence number
                EXPECT_EQ(msg->timestamp_ns, msg->seq * 1'000'000ULL);
                received_sequences.push_back(msg->seq);
                consumer.pop();
            }
            else {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#elif defined(__aarch64__) || defined(__arm64__)
                asm volatile("yield");
#endif
            }
        }
    });

    // Producer thread: streams EventCount events, spinning when full
    std::thread producer_thread([&]() {
        SpscProducer<TestMessage, Capacity> producer(layout.get());
        for (size_t i = 0; i < MessageCount; ++i) {
            TestMessage msg = make_message(static_cast<uint32_t>(i));

            while (!producer.try_push(msg)) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#elif defined(__aarch64__) || defined(__arm64__)
                asm volatile("yield");
#endif
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    producer_thread.join();
    consumer_thread.join();

    // Verify all events received in strict order with zero loss
    ASSERT_EQ(received_sequences.size(), MessageCount);
    for (size_t i = 0; i < MessageCount; ++i) {
        EXPECT_EQ(received_sequences[i], static_cast<uint32_t>(i));
    }
}

// 7. Dropped Event Tracking
TEST(IpcSpscQueueTest, TracksDroppedEventsWhenFull) {
    constexpr size_t Capacity = 4;
    auto layout = std::make_unique<detail::RingBufferLayout<TestMessage, Capacity>>();
    SpscProducer<TestMessage, Capacity> producer(layout.get());
    SpscConsumer<TestMessage, Capacity> consumer(layout.get());

    EXPECT_EQ(consumer.dropped_count(), 0);

    for (size_t i = 0; i < Capacity; ++i) {
        EXPECT_TRUE(producer.try_push(make_message(static_cast<uint32_t>(i))));
    }

    // Now pushes should fail and increment dropped_events
    EXPECT_FALSE(producer.try_push(make_message(100)));
    EXPECT_FALSE(producer.try_push(make_message(101)));
    EXPECT_EQ(consumer.dropped_count(), 2);

    // Drain one slot via zero-copy front() + pop()
    const auto* front_item = consumer.front();
    ASSERT_NE(front_item, nullptr);
    EXPECT_EQ(front_item->seq, 0);
    consumer.pop();

    // Now pushing succeeds once
    EXPECT_TRUE(producer.try_push(make_message(102)));
    EXPECT_EQ(consumer.dropped_count(), 2); // Dropped count remains 2
}

// 8. POSIX Shared Memory Lifecycle
TEST(IpcSpscQueueTest, ShmProducerConsumerLifecycle) {
    constexpr size_t Capacity = 64;
    const std::string test_shm = "/test_quinnfra_ipc_" + std::to_string(::getpid());

    // Consumer attach fails if producer has not created it
    auto consumer_before_producer = SpscConsumer<TestMessage, Capacity>::attach(test_shm);
    EXPECT_FALSE(consumer_before_producer.has_value());

    // Producer creates and owns the shared memory
    auto producer_opt = SpscProducer<TestMessage, Capacity>::create(test_shm);
    ASSERT_TRUE(producer_opt.has_value());

    // Consumer can now attach to the existing segment
    auto consumer_opt = SpscConsumer<TestMessage, Capacity>::attach(test_shm);
    ASSERT_TRUE(consumer_opt.has_value());

    // Data flows across the shared memory
    EXPECT_TRUE(producer_opt->try_push(make_message(42)));
    const auto* msg1 = consumer_opt->front();
    ASSERT_NE(msg1, nullptr);
    EXPECT_EQ(msg1->seq, 42);
    consumer_opt->pop();

    // Push more data to the alive segment
    EXPECT_TRUE(producer_opt->try_push(make_message(43)));

    // New consumer attaches and resumes reading seamlessly
    auto new_consumer = SpscConsumer<TestMessage, Capacity>::attach(test_shm);
    ASSERT_TRUE(new_consumer.has_value());
    const auto* msg2 = new_consumer->front();
    ASSERT_NE(msg2, nullptr);
    EXPECT_EQ(msg2->seq, 43);
    new_consumer->pop();

    // Producer destruction unlinks the shared memory
    producer_opt.reset();
    new_consumer.reset();

    // After producer destruction, attach should fail
    auto consumer_after_cleanup = SpscConsumer<TestMessage, Capacity>::attach(test_shm);
    EXPECT_FALSE(consumer_after_cleanup.has_value());
}

} // namespace quinnfra::ipc::testing
