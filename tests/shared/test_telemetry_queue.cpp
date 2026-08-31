#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#include "shared/telemetry_event.hpp"
#include "shared/telemetry_payloads.hpp"
#include "shared/telemetry_queue.hpp"

namespace telemetry::testing {

// Helper to construct deterministic TelemetryEvent instances matching telemetry_event.hpp
TelemetryEvent make_event(uint32_t seq, EventType type = EventType::HEARTBEAT) {
    TelemetryEvent ev{};
    ev.timestamp_ns = 1'000'000ULL * seq;
    ev.sequence_num = seq;
    ev.source_id = SourceId::UNKNOWN;
    ev.level = LogLevel::INFO;
    ev.type = type;
    ev.payload.heartbeat = HeartbeatPayload{};
    return ev;
}


// 1. Memory Layout & Cache Line False-Sharing Checks
TEST(SpscQueueTest, MemoryLayoutAndCacheAlignment) {
    using TestQueue = detail::RingBufferLayout<TelemetryEvent, 1024>;

    // Ensure the struct and its critical members meet the 64-byte alignment requirement
    EXPECT_EQ(alignof(TestQueue), hardware_destructive_interference_size);
    EXPECT_EQ(offsetof(TestQueue, write_index) % hardware_destructive_interference_size, 0);
    EXPECT_EQ(offsetof(TestQueue, read_index) % hardware_destructive_interference_size, 0);
    EXPECT_EQ(offsetof(TestQueue, buffer) % hardware_destructive_interference_size, 0);

    // Ensure write_index and read_index reside on strictly separate cache lines
    size_t index_distance = offsetof(TestQueue, read_index) - offsetof(TestQueue, write_index);
    EXPECT_GE(index_distance, hardware_destructive_interference_size);
}

// 2. Empty Queue Behavior
TEST(SpscQueueTest, EmptyQueueReturnsNullptr) {
    constexpr size_t Capacity = 16;

    auto layout = std::make_unique<detail::RingBufferLayout<TelemetryEvent, Capacity>>();
    detail::ConsumerView<TelemetryEvent, Capacity> consumer(layout.get());

    EXPECT_EQ(consumer.front(), nullptr);
    EXPECT_EQ(consumer.front(), nullptr);
}

// 3. FIFO Ordering, Peek/Pop Verification
TEST(SpscQueueTest, FifoOrderingAndPeekPopContract) {
    constexpr size_t Capacity = 8;

    auto layout = std::make_unique<detail::RingBufferLayout<TelemetryEvent, Capacity>>();
    detail::ProducerView<TelemetryEvent, Capacity> producer(layout.get());
    detail::ConsumerView<TelemetryEvent, Capacity> consumer(layout.get());

    auto ev1 = make_event(101, EventType::HEARTBEAT);
    auto ev2 = make_event(102, EventType::HEARTBEAT);

    EXPECT_TRUE(producer.try_push(ev1));
    EXPECT_TRUE(producer.try_push(ev2));

    // Peek first event: front() must not advance the read pointer
    const TelemetryEvent* peek1 = consumer.front();
    ASSERT_NE(peek1, nullptr);
    EXPECT_EQ(peek1->sequence_num, 101);
    EXPECT_EQ(peek1->type, EventType::HEARTBEAT);

    const TelemetryEvent* peek1_again = consumer.front();
    EXPECT_EQ(peek1, peek1_again);

    // Pop first event
    consumer.pop();

    // Now front() should point to the second event
    const TelemetryEvent* peek2 = consumer.front();
    ASSERT_NE(peek2, nullptr);
    EXPECT_EQ(peek2->sequence_num, 102);
    EXPECT_EQ(peek2->type, EventType::HEARTBEAT);

    consumer.pop();
    EXPECT_EQ(consumer.front(), nullptr);
}

// 4. Capacity Bound and Event Dropping
TEST(SpscQueueTest, RejectsPushWhenBufferIsFull) {
    constexpr size_t Capacity = 4;

    auto layout = std::make_unique<detail::RingBufferLayout<TelemetryEvent, Capacity>>();
    detail::ProducerView<TelemetryEvent, Capacity> producer(layout.get());
    detail::ConsumerView<TelemetryEvent, Capacity> consumer(layout.get());

    // Fill the buffer to capacity
    for (size_t i = 0; i < Capacity; ++i) {
        EXPECT_TRUE(producer.try_push(make_event(static_cast<uint32_t>(i))));
    }

    // Next push MUST fail cleanly without overwriting or hanging
    EXPECT_FALSE(producer.try_push(make_event(999)));

    // Drain one slot
    ASSERT_NE(consumer.front(), nullptr);
    EXPECT_EQ(consumer.front()->sequence_num, 0);
    consumer.pop();

    // Now pushing should succeed once
    EXPECT_TRUE(producer.try_push(make_event(100)));
    EXPECT_FALSE(producer.try_push(make_event(101)));
}

// 5. Index Wrap-Around
TEST(SpscQueueTest, ContinuousWrapAroundIntegrity) {
    constexpr size_t Capacity = 8;
    constexpr size_t TotalEvents = 100'000;

    auto layout = std::make_unique<detail::RingBufferLayout<TelemetryEvent, Capacity>>();
    detail::ProducerView<TelemetryEvent, Capacity> producer(layout.get());
    detail::ConsumerView<TelemetryEvent, Capacity> consumer(layout.get());

    for (size_t i = 0; i < TotalEvents; ++i) {
        ASSERT_TRUE(producer.try_push(make_event(static_cast<uint32_t>(i))));

        const TelemetryEvent* item = consumer.front();
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->sequence_num, static_cast<uint32_t>(i));
        consumer.pop();
    }

    EXPECT_EQ(consumer.front(), nullptr);
    EXPECT_EQ(layout->write_index.load(), TotalEvents);
    EXPECT_EQ(layout->read_index.load(), TotalEvents);
}

// 6. Concurrent Multi-Threaded Stress Test
TEST(SpscQueueTest, ConcurrentStreamingNoLossOrCorruption) {
    constexpr size_t Capacity = 1024;
    constexpr size_t EventCount = 2'000'000;

    auto layout = std::make_unique<detail::RingBufferLayout<TelemetryEvent, Capacity>>();
    std::atomic<bool> producer_done{false};

    std::vector<uint32_t> received_sequences;
    received_sequences.reserve(EventCount);

    // Consumer thread: reads until producer is done and queue is drained
    std::thread consumer_thread([&]() {
        detail::ConsumerView<TelemetryEvent, Capacity> consumer(layout.get());

        while (!producer_done.load(std::memory_order_relaxed) || consumer.front() != nullptr) {
            if (const auto* ev = consumer.front()) {
                // Verify payload integrity against sequence number
                EXPECT_EQ(ev->timestamp_ns, ev->sequence_num * 1'000'000ULL);
                received_sequences.push_back(ev->sequence_num);
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
        detail::ProducerView<TelemetryEvent, Capacity> producer(layout.get());

        for (size_t i = 0; i < EventCount; ++i) {
            TelemetryEvent ev = make_event(static_cast<uint32_t>(i));
            
            while (!producer.try_push(ev)) {
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
    ASSERT_EQ(received_sequences.size(), EventCount);
    for (size_t i = 0; i < EventCount; ++i) {
        ASSERT_EQ(received_sequences[i], static_cast<uint32_t>(i));
    }
}

} // namespace telemetry::testing
