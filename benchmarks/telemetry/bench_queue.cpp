#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include <benchmark/benchmark.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm64__)
#define CPU_PAUSE() asm volatile("yield")
#else
#define CPU_PAUSE() ((void)0)
#endif

#include <quinnfra/telemetry/consumer.hpp>
#include <quinnfra/telemetry/producer.hpp>

namespace telemetry::benchmarking {

struct alignas(64) BenchEvent {
    uint64_t timestamp_ns{0};
    uint32_t sequence_num{0};
    uint16_t source_id{1};
    uint8_t  level{1};
    uint8_t  type{0};
    uint8_t  payload[48]{0};
};

static_assert(sizeof(BenchEvent) == 64, "BenchEvent must by 64 bytes");
static_assert(std::is_trivially_copyable_v<BenchEvent>, "BenchEvent must be trivially copyable");

constexpr size_t DefaultCapacity = telemetry::DEFAULT_QUEUE_CAPACITY;
constexpr size_t BatchChunkSize  = DefaultCapacity / 4; // 16,384
constexpr size_t BatchMask       = BatchChunkSize - 1;  // 0x3FFF


// 1. Producer try_push() Latency
void BM_Producer_TryPush_Latency(benchmark::State& state) {
    auto layout = std::make_unique<telemetry::detail::RingBufferLayout<BenchEvent, DefaultCapacity>>();
    telemetry::ProducerView<BenchEvent, DefaultCapacity> producer(layout.get());
    telemetry::ConsumerView<BenchEvent, DefaultCapacity> consumer(layout.get());

    BenchEvent ev{};
    uint32_t seq = 0;

    for (auto _ : state) {
        ev.sequence_num = ++seq;
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
    auto layout = std::make_unique<telemetry::detail::RingBufferLayout<BenchEvent, DefaultCapacity>>();
    telemetry::ProducerView<BenchEvent, DefaultCapacity> producer(layout.get());
    telemetry::ConsumerView<BenchEvent, DefaultCapacity> consumer(layout.get());

    BenchEvent ev{};
    uint32_t seq = 0;

    // Helper lambda to fill queue
    auto fill_queue = [&]() {
        for (size_t i = 0; i < BatchChunkSize; ++i) {
            ev.sequence_num = ++seq;
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
    auto layout = std::make_unique<telemetry::detail::RingBufferLayout<BenchEvent, DefaultCapacity>>();
    telemetry::ProducerView<BenchEvent, DefaultCapacity> producer(layout.get());
    telemetry::ConsumerView<BenchEvent, DefaultCapacity> consumer(layout.get());

    BenchEvent ev{};
    uint32_t seq = 0;

    for (auto _ : state) {
        ev.sequence_num = ++seq;
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
    std::unique_ptr<telemetry::detail::RingBufferLayout<BenchEvent, DefaultCapacity>> layout;
    std::unique_ptr<telemetry::ProducerView<BenchEvent, DefaultCapacity>> producer;
    std::unique_ptr<telemetry::ConsumerView<BenchEvent, DefaultCapacity>> consumer;
    std::atomic<bool> producer_active{false};

    ConcurrentFixture() {
        reset();
    }

    void reset() {
        layout = std::make_unique<telemetry::detail::RingBufferLayout<BenchEvent, DefaultCapacity>>();
        producer = std::make_unique<telemetry::ProducerView<BenchEvent, DefaultCapacity>>(layout.get());
        consumer = std::make_unique<telemetry::ConsumerView<BenchEvent, DefaultCapacity>>(layout.get());
        producer_active.store(false, std::memory_order_relaxed);
    }
};

static ConcurrentFixture g_concurrent_fixture;

void BM_SPSC_Concurrent_Throughput(benchmark::State& state) {
    if (state.thread_index() == 0) {
        // Producer thread
        g_concurrent_fixture.producer_active.store(true, std::memory_order_release);
        BenchEvent ev{};
        uint32_t seq = 0;

        for (auto _ : state) {
            ev.sequence_num = ++seq;
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

} // namespace telemetry::benchmarking
