#pragma once
#include <cstdint>
#include <ostream>
#include <string_view>

#include <quinnfra/telemetry/event.hpp>

namespace telemetry {

/// @brief Returns the human-readable name of a SourceId.
constexpr std::string_view name_of(SourceId source) noexcept {
    switch (source) {
        case SourceId::UNKNOWN: return "UNKNOWN";
        default:                return "CUSTOM";
    }
}

/// @brief Returns the human-readable name of a LogLevel.
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

/// @brief Returns the human-readable name of an EventType.
constexpr std::string_view name_of(EventType type) noexcept {
    switch (type) {
        case EventType::HEARTBEAT: return "HEARTBEAT";
        default:                   return "UNKNOWN";
    }
}

/// @brief Stream insertion operator for LogLevel.
inline std::ostream& operator<<(std::ostream& os, LogLevel level) {
    return os << name_of(level);
}

/// @brief Stream insertion operator for EventType.
inline std::ostream& operator<<(std::ostream& os, EventType type) {
    return os << name_of(type);
}

/// @brief Stream insertion operator for SourceId.
inline std::ostream& operator<<(std::ostream& os, SourceId source) {
    return os << name_of(source);
}

} // namespace telemetry
