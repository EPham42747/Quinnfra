#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm64__)
#define CPU_PAUSE() asm volatile("yield")
#else
#define CPU_PAUSE() ((void)0)
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include <benchmark/benchmark.h>

#include <quinnfra/core/alignment.hpp>
#include <quinnfra/ipc/spsc_consumer.hpp>
#include <quinnfra/ipc/spsc_producer.hpp>

namespace quinnfra::ipc::benchmarking {

struct alignas(64) BenchMessage {
    uint64_t timestamp_ns{0};
    uint32_t seq{0};
    uint8_t  payload[52]{0};
};

static_assert(sizeof(BenchMessage) == 64, "BenchMessage must be 64 bytes");
static_assert(std::is_trivially_copyable_v<BenchMessage>, "BenchMessage must be trivially copyable");

constexpr size_t DefaultCapacity = quinnfra::ipc::DEFAULT_QUEUE_CAPACITY;
constexpr size_t BatchChunkSize  = DefaultCapacity / 4; // 16,384
constexpr size_t BatchMask       = BatchChunkSize - 1;  // 0x3FFF

// 1. Producer try_push() Latency
void BM_Producer_TryPush_Latency(benchmark::State& state) {
    auto layout = std::make_unique<detail::RingBufferLayout<BenchMessage, DefaultCapacity>>();
    SpscProducer<BenchMessage, DefaultCapacity> producer(layout.get());
    SpscConsumer<BenchMessage, DefaultCapacity> consumer(layout.get());

    BenchMessage ev{};
    uint32_t seq = 0;

    for (auto _ : state) {
        ev.seq = ++seq;
        benchmark::DoNotOptimize(producer.try_push(ev));

        // Periodically drain without counting drain time in latency measurement
        if ((seq & BatchMask) == 0) {
            state.PauseTiming();
            while (consumer.front()) {
                consumer.pop();
            }
            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Producer_TryPush_Latency);

// 2. Consumer front() + pop() Latency
void BM_Consumer_FrontAndPop_Latency(benchmark::State& state) {
    auto layout = std::make_unique<detail::RingBufferLayout<BenchMessage, DefaultCapacity>>();
    SpscProducer<BenchMessage, DefaultCapacity> producer(layout.get());
    SpscConsumer<BenchMessage, DefaultCapacity> consumer(layout.get());

    BenchMessage ev{};
    uint32_t seq = 0;

    // Helper lambda to fill queue
    auto fill_queue = [&]() {
        for (size_t i = 0; i < BatchChunkSize; ++i) {
            ev.seq = ++seq;
            (void)producer.try_push(ev);
        }
    };

    fill_queue();

    for (auto _ : state) {
        const auto* item = consumer.front();
        benchmark::DoNotOptimize(item);
        consumer.pop();

        // Refill if getting low, pausing measurement
        if (consumer.front() == nullptr) {
            state.PauseTiming();
            fill_queue();
            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Consumer_FrontAndPop_Latency);

// 3. Round-Trip try_push() + pop() Latency
void BM_Queue_PushAndPop_Latency(benchmark::State& state) {
    auto layout = std::make_unique<detail::RingBufferLayout<BenchMessage, DefaultCapacity>>();
    SpscProducer<BenchMessage, DefaultCapacity> producer(layout.get());
    SpscConsumer<BenchMessage, DefaultCapacity> consumer(layout.get());

    BenchMessage ev{};
    uint32_t seq = 0;

    for (auto _ : state) {
        ev.seq = ++seq;
        benchmark::DoNotOptimize(producer.try_push(ev));
        const auto* item = consumer.front();
        benchmark::DoNotOptimize(item);
        consumer.pop();
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Queue_PushAndPop_Latency);

// 4. Queue Throughput
struct ConcurrentFixture {
    std::unique_ptr<detail::RingBufferLayout<BenchMessage, DefaultCapacity>> layout;
    std::unique_ptr<SpscProducer<BenchMessage, DefaultCapacity>> producer;
    std::unique_ptr<SpscConsumer<BenchMessage, DefaultCapacity>> consumer;
    std::atomic<bool> producer_active{false};

    ConcurrentFixture() {
        reset();
    }

    void reset() {
        layout = std::make_unique<detail::RingBufferLayout<BenchMessage, DefaultCapacity>>();
        producer = std::make_unique<SpscProducer<BenchMessage, DefaultCapacity>>(layout.get());
        consumer = std::make_unique<SpscConsumer<BenchMessage, DefaultCapacity>>(layout.get());
        producer_active.store(false, std::memory_order_relaxed);
    }
};

static ConcurrentFixture g_concurrent_fixture;

void BM_SPSC_Concurrent_Throughput(benchmark::State& state) {
    if (state.thread_index() == 0) {
        // Producer thread
        g_concurrent_fixture.producer_active.store(true, std::memory_order_release);
        BenchMessage ev{};
        uint32_t seq = 0;

        for (auto _ : state) {
            ev.seq = ++seq;
            while (!g_concurrent_fixture.producer->try_push(ev)) {
                CPU_PAUSE();
            }
        }

        g_concurrent_fixture.producer_active.store(false, std::memory_order_release);
        state.SetItemsProcessed(state.iterations());
    } else {
        // Consumer thread
        uint64_t consumed = 0;
        for (auto _ : state) {
            while (true) {
                if (const auto* item = g_concurrent_fixture.consumer->front()) {
                    benchmark::DoNotOptimize(item);
                    g_concurrent_fixture.consumer->pop();
                    ++consumed;
                    break;
                }

                if (!g_concurrent_fixture.producer_active.load(std::memory_order_acquire)) {
                    // Above front() may have failed first, then producer pushed last item
                    // Check queue one last time before exiting
                    if (const auto* item = g_concurrent_fixture.consumer->front()) {
                        benchmark::DoNotOptimize(item);
                        g_concurrent_fixture.consumer->pop();
                        ++consumed;
                    }
                    break;
                }

                CPU_PAUSE();
            }
        }
        state.SetItemsProcessed(consumed);
    }
}
BENCHMARK(BM_SPSC_Concurrent_Throughput)->Threads(2);

} // namespace quinnfra::ipc::benchmarking
