#pragma once
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace examples {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};

enum class EventType : uint8_t {
    HEARTBEAT    = 0
};

/// @brief Example 64-byte unified event struct.
struct alignas(64) ExampleEvent {
    // 16-byte Header
    uint64_t  timestamp_ns{0};
    uint32_t  sequence_num{0};
    uint16_t  source_id{0};
    LogLevel  level{LogLevel::INFO};
    EventType type{EventType::HEARTBEAT};

    // 48-byte Payload (unformatted/raw storage, application-defined)
    uint8_t   payload[48]{0};
};

constexpr std::string_view name_of(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "UNKNOWN";
    }
}

constexpr std::string_view name_of(EventType type) noexcept {
    switch (type) {
        case EventType::HEARTBEAT: return "HEARTBEAT";
        default:                   return "UNKNOWN";
    }
}

static_assert(sizeof(ExampleEvent) == 64, "ExampleEvent must be exactly 64 bytes (1 cache line)");
static_assert(std::is_trivially_copyable_v<ExampleEvent>, "ExampleEvent must be trivially copyable");

} // namespace examples
