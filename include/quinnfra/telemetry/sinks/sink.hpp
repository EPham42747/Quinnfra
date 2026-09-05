#pragma once
#include <cstddef>
#include <quinnfra/telemetry/event.hpp>

namespace telemetry {

/// @brief Abstract base class for telemetry event consumers and exporters.
class Sink {
public:
    virtual ~Sink() = default;

    /// @brief Consume a single telemetry event.
    /// @param event The telemetry event to process.
    virtual void write(const TelemetryEvent& event) = 0;

    /// @brief Optional batch consumption for high-throughput sinks.
    /// @details By default, delegates to write() in a loop. Sinks that can
    /// amortize I/O (e.g. writev or bulk network sends) can override this.
    /// @param events Pointer to contiguous array of events.
    /// @param count Number of events in the batch.
    virtual void write(const TelemetryEvent* events, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            write(events[i]);
        }
    }

    /// @brief Flushes any buffered data to the underlying destination.
    virtual void flush() {}
};

} // namespace telemetry
