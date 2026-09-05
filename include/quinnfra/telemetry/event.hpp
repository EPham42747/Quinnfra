#pragma once
#include <cstdint>
#include <type_traits>

#include "payloads.hpp"

/// @file event.hpp
/// @brief IPC telemetry event definitions and metadata.
///
/// @details
/// To add new events:
///     - Create a new payload in `payloads.hpp`
///     - Map the payload into the `payload` union
///     - Add a unique identifier to `EventType`
///     - Optionally, add human name to `name_of(EventType)` in `format.hpp`
///     - Ensure static assertions for size and trivial copyability pass

namespace telemetry {

/// @brief Subsystem or engine identifier indicating the origin of an event.
/// @note When creating a new event producer, add its identifier here.
enum class SourceId : uint16_t {
    UNKNOWN = 0
};

/// @brief Severity level of the telemetry event.
enum class LogLevel : uint8_t {
    DEBUG = 0, ///< Diagnostic details for development/debugging.
    INFO  = 1, ///< Standard operational information.
    WARN  = 2, ///< Non-critical anomaly; execution continues safely.
    ERROR = 3, ///< Recoverable failure or unexpected condition.
    FATAL = 4  ///< Critical failure requiring immediate termination.
};

/// @brief Discriminator defining the event and payload type.
/// @note When creating a new event, add a corresponding identifier here.
enum class EventType : uint8_t {
    HEARTBEAT = 0
};

/// @brief Fixed-size 64-byte unified telemetry event.
///
/// @details
/// Memory layout:
///     - 16 bytes: Header metadata (`timestamp_ns`, `sequence_num`, etc.)
///     - 48 bytes: Tagged Payload Union (`payload`)
///     - Totals exactly 64 bytes (L1 CPU data cache line size)
struct alignas(64) TelemetryEvent {
    // Header
    uint64_t  timestamp_ns;
    uint32_t  sequence_num;  // Monotonic increasing counter, maintained per producer
    SourceId  source_id;
    LogLevel  level;
    EventType type;

    // Payload
    union {
        HeartbeatPayload heartbeat;
        uint8_t          raw_bytes[48];
    } payload;
};

// Static assertions guarantee zero dynamic overhead and cache-line fit
static_assert(sizeof(TelemetryEvent) == 64, "TelemetryEvent must be exactly 64 bytes (1 cache line)");
static_assert(std::is_trivially_copyable<TelemetryEvent>::value, "TelemetryEvent must be trivially copyable for direct binary copy");

} // namespace telemetry
