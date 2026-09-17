#pragma once
#include <cstdint>
#include <type_traits>

namespace quinnfra::telemetry::testing {

enum class TestLogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};

enum class TestEventType : uint8_t {
    HEARTBEAT = 0,
    METRIC    = 1,
    LOG       = 2
};

struct alignas(64) TestEvent {
    uint64_t      timestamp_ns{0};
    uint32_t      sequence_num{0};
    uint16_t      source_id{0};
    TestLogLevel  level{TestLogLevel::INFO};
    TestEventType type{TestEventType::HEARTBEAT};
    uint8_t       payload[48]{};
};

static_assert(sizeof(TestEvent) == 64, "TestEvent must be 64 bytes");
static_assert(std::is_trivially_copyable_v<TestEvent>, "TestEvent must be trivially copyable");

} // namespace quinnfra::telemetry::testing
