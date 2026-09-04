#pragma once
#include <cstdint>

/// @file telemetry_payloads.hpp
/// @brief Payload definitions for telemetry events.
///
/// @details
/// New payloads:
///     - Must be trivially copyable
///     - Must NOT use dynamic memory (e.g., std::vector, heap pointers)
///     - Must NOT exceed 48 bytes in total size

namespace telemetry {

/// @brief Empty payload for heartbeat (wrapping event contains timestamp).
struct HeartbeatPayload {};

} // namespace telemetry
