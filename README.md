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
```

## Developer Guide

### Producing Telemetry from an Engine

Include the `producer.hpp` header:
```cpp
#include <quinnfra/telemetry/producer.hpp>
```

The `TelemetryProducer` type includes a static factory that creates a queue in shared memory. It returns `std::optional` because it may fail, so ensure you validate before using the returned `TelemetryProducer`.
```cpp
auto producer = telemetry::TelemetryProducer::create("/sample_path");
if (!producer.has_value()) {
    // Handle error
}
```

Create an event and call `try_push()`:
```cpp
telemetry::TelemetryEvent ev{};
ev.timestamp_ns = getCurrentNanos();
ev.sequence_num = seq++;
ev.source_id    = telemetry::SourceId::UNKNOWN;
ev.level        = telemetry::LogLevel::INFO;
ev.type         = telemetry::EventType::HEARTBEAT;

if (!producer->try_push(ev)) {
    // Queue is full
}
```

### Consuming Telemetry from a Worker

Include the `consumer.hpp` header:
```cpp
#include <quinnfra/telemetry/consumer.hpp>
```

The `TelemetryConsumer` type includes a static factory that attaches to an existing queue created by a producer. This will fail if the queue was not created prior. Always validate before using:
```cpp
auto consumer = telemetry::TelemetryConsumer::attach("/sample_path");
if (!consumer.has_value()) {
    // Handle error
}
```

Add a sink to handle events that the consumer collects. You can either use a prebuilt one or extend the `telemetry::Sink` interface.
```cpp
auto sink = std::make_unique<telemetry::TextFileSink>("events.log");
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

### Using the Pre-Built Daemon

Quinnfra provides `telemetry_daemon`, a pre-built consumer with CLI configuration and graceful `Ctrl+C` shutdown:

```bash
./build/telemetry_daemon /sample_path \
    --text-log events.log \
    --error-log errors.log \
    --binary-log events.bin
```
