# Quinnfra

A zero-dependency, low-latency C++20 telemetry library designed for high-frequency trading (HFT) systems.

Quinnfra provides an inter-process telemetry queue powered by a lock-free, Single-Producer-Single-Consumer (SPSC), shared-memory ring buffer. It allows trading engines and other critical processes to emit telemetry with sub-microsecond overhead, offloading formatting, filtering, and disk I/O to a background daemon.

Under active development.

## Setup

Prerequisites: C++20 compiler, CMake 3.20+, POSIX-compliant OS

```bash
git clone https://github.com/EPham42747/Quinnfra.git
cd Quinnfra
cmake -B build
cmake --build build
```

## Performance

* **Producer `try_push()` Overhead:** **~1.8 ns** (~560M events/sec)
* **Concurrent SPSC Throughput:** **~191M events/sec** (~12.2 GB/s)
* **Jitter & Tail Predictability:** Standard deviation <0.1 ns (<3.5% CV)

See [**Benchmarks**](benchmarks/README.md) for more detail.

## Developer Guide

### Define an Event

Define your own event schema:
```cpp
struct alignas(64) Event {
    uint64_t timestamp_ns;
    uint32_t sequence_num;
    uint16_t source_id;
    uint8_t  level;
    uint8_t  type;
    uint8_t  payload[48];
};

static_assert(sizeof(Event) == 64, "Event must fit in a 64-byte cache line");
static_assert(std::is_trivially_copyable_v<Event>, "Event must be trivially copyable");
```

### Producing Telemetry from an Engine

Include the `producer.hpp` header:
```cpp
#include <quinnfra/telemetry/producer.hpp>
```

The `TelemetryProducer` type includes a static factory that creates a queue in shared memory. It returns `std::optional` because it may fail, so ensure you validate before using the returned `TelemetryProducer`.
```cpp
auto producer = telemetry::TelemetryProducer<Event>::create("/sample_path");
if (!producer.has_value()) {
    // Handle error
}
```

Create an event and call `try_push()`:
```cpp
Event ev{};
ev.timestamp_ns = getCurrentNanos();
ev.sequence_num = seq++;
ev.source_id    = 1;
ev.level        = 1;
ev.type         = 0;

if (!producer->try_push(ev)) {
    // Queue is full
}
```

### Consuming Telemetry in a Consumer Process

Include the `consumer.hpp` and any desired sink headers:
```cpp
#include <quinnfra/telemetry/consumer.hpp>
#include <quinnfra/telemetry/sinks/text_file_sink.hpp>
```

The `TelemetryConsumer` type includes a static factory that attaches to an existing queue created by a producer. This will fail if the queue was not created prior. Always validate before using:
```cpp
auto consumer = telemetry::TelemetryConsumer<Event>::attach("/sample_path");
if (!consumer.has_value()) {
    // Handle error
}
```

Add a sink to handle events that the consumer collects. You can either use a prebuilt one or extend the `telemetry::Sink` interface.
```cpp
auto formatter = [](const Event& event) {
    return "[" + std::to_string(event.timestamp_ns) + " ns] "
         + "[SEQ:" + std::to_string(event.sequence_num) + "]\n";
};
auto sink = std::make_unique<telemetry::TextFileSink<Event>>("sample.log", formatter);
```

Check for new events with `front()` and release them with `pop()`:
```cpp
while (running) {
    if (const auto* event = consumer->front()) {
        sink->write(*event);
        consumer->pop();
    }
    else {
        sink->flush();
        std::this_thread::yield();
    }
}
```
