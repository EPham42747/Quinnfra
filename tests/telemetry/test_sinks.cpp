#include <cstdint>
#include <fstream>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

#include <quinnfra/telemetry/event.hpp>
#include <quinnfra/telemetry/payloads.hpp>
#include <quinnfra/telemetry/sinks/binary_file_sink.hpp>

namespace telemetry::testing {

namespace {
TelemetryEvent make_event(uint32_t seq, EventType type = EventType::HEARTBEAT) {
    TelemetryEvent ev{};
    ev.timestamp_ns = 1'000'000ULL * seq;
    ev.sequence_num = seq;
    ev.source_id = SourceId::UNKNOWN;
    ev.level = LogLevel::INFO;
    ev.type = type;
    ev.payload.heartbeat = HeartbeatPayload{};
    return ev;
}
} // namespace

// 1. Binary File Sink (Raw 64-byte binary serialization and replay)
TEST(TelemetrySinkTest, BinaryFileSinkWritesExactBinaryRecords) {
    const std::string filepath = "test_telemetry_sink_" + std::to_string(::getpid()) + ".bin";

    // Scope the sink so it flushes and closes
    {
        BinaryFileSink sink(filepath, false);
        ASSERT_TRUE(sink.is_open());

        auto ev1 = make_event(1001);
        auto ev2 = make_event(1002);
        sink.write(ev1);
        sink.write(ev2);

        // Test batch write
        TelemetryEvent batch[2] = {make_event(1003), make_event(1004)};
        sink.write(batch, 2);
    }

    // Read back and verify exact byte alignment and contents
    std::ifstream file(filepath, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    TelemetryEvent read_events[4]{};
    file.read(reinterpret_cast<char*>(read_events), sizeof(read_events));
    EXPECT_EQ(file.gcount(), static_cast<std::streamsize>(4 * sizeof(TelemetryEvent)));

    EXPECT_EQ(read_events[0].sequence_num, 1001);
    EXPECT_EQ(read_events[1].sequence_num, 1002);
    EXPECT_EQ(read_events[2].sequence_num, 1003);
    EXPECT_EQ(read_events[3].sequence_num, 1004);

    file.close();
    std::remove(filepath.c_str());
}

} // namespace telemetry::testing
