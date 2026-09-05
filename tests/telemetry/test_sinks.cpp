#include <cstdint>
#include <fstream>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

#include <quinnfra/telemetry/event.hpp>
#include <quinnfra/telemetry/payloads.hpp>
#include <quinnfra/telemetry/sinks/binary_file_sink.hpp>
#include <quinnfra/telemetry/sinks/text_file_sink.hpp>
#include <quinnfra/telemetry/sinks/filtered_sink.hpp>

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

// 2. Text File Sink (Human-readable log line formatting)
TEST(TelemetrySinkTest, TextFileSinkFormatsReadableLines) {
    const std::string filepath = "test_telemetry_sink_" + std::to_string(::getpid()) + ".log";

    {
        TextFileSink sink(filepath, /*append=*/false);
        ASSERT_TRUE(sink.is_open());

        auto ev1 = make_event(1001);
        ev1.level = LogLevel::INFO;

        auto ev2 = make_event(1002);
        ev2.level = LogLevel::ERROR;

        sink.write(ev1);
        sink.write(ev2);
    }

    std::ifstream file(filepath);
    ASSERT_TRUE(file.is_open());

    std::string line1;
    std::string line2;
    ASSERT_TRUE(std::getline(file, line1));
    ASSERT_TRUE(std::getline(file, line2));

    EXPECT_EQ(line1, "[1001000000 ns] [INFO] [SRC:UNKNOWN] [SEQ:1001] [HEARTBEAT]");
    EXPECT_EQ(line2, "[1002000000 ns] [ERROR] [SRC:UNKNOWN] [SEQ:1002] [HEARTBEAT]");

    file.close();
    std::remove(filepath.c_str());
}

// 3. Filtered Sink with Custom FilterRule (Multi-field filtering)
TEST(TelemetrySinkTest, FilteredSinkCustomRuleMatchesMultipleFields) {
    const std::string filepath = "test_telemetry_custom_filter_" + std::to_string(::getpid()) + ".log";

    {
        // Rule: Only keep events with odd sequence numbers AND level == LogLevel::INFO
        FilteredSink::FilterRule custom_rule = [](const TelemetryEvent& ev) {
            return (ev.sequence_num % 2 != 0) && (ev.level == LogLevel::INFO);
        };

        auto inner = std::make_unique<TextFileSink>(filepath, false);
        FilteredSink filter(std::move(custom_rule), std::move(inner));

        auto ev1 = make_event(3001); // odd, INFO -> KEEP
        auto ev2 = make_event(3002); // even, INFO -> DISCARD
        auto ev3 = make_event(3003); // odd, WARN -> DISCARD
        ev3.level = LogLevel::WARN;
        auto ev4 = make_event(3005); // odd, INFO -> KEEP

        filter.write(ev1);
        filter.write(ev2);
        filter.write(ev3);
        filter.write(ev4);
    }

    std::ifstream file(filepath);
    ASSERT_TRUE(file.is_open());

    std::string line1;
    std::string line2;
    std::string line3;

    ASSERT_TRUE(std::getline(file, line1));
    ASSERT_TRUE(std::getline(file, line2));
    EXPECT_FALSE(std::getline(file, line3));

    EXPECT_EQ(line1, "[3001000000 ns] [INFO] [SRC:UNKNOWN] [SEQ:3001] [HEARTBEAT]");
    EXPECT_EQ(line2, "[3005000000 ns] [INFO] [SRC:UNKNOWN] [SEQ:3005] [HEARTBEAT]");

    file.close();
    std::remove(filepath.c_str());
}

// 4. Filtered Sink Batch Write (Verifies run-length slicing across non-contiguous matches)
TEST(TelemetrySinkTest, FilteredSinkBatchRunLengthSlicing) {
    const std::string filepath = "test_telemetry_batch_filter_" + std::to_string(::getpid()) + ".bin";

    {
        auto inner = std::make_unique<BinaryFileSink>(filepath, false);
        FilteredSink filter([](const TelemetryEvent& ev) { return ev.level >= LogLevel::INFO; }, std::move(inner));

        TelemetryEvent batch[6] = {
            make_event(4001), // INFO -> PASS (run 1 start)
            make_event(4002), // INFO -> PASS (run 1 len=2)
            make_event(4003), // DEBUG -> DROP (run 1 flushed, hole)
            make_event(4004), // INFO -> PASS (run 2 start)
            make_event(4005), // INFO -> PASS (run 2 len=2)
            make_event(4006)  // DEBUG -> DROP (run 2 flushed, hole)
        };
        batch[2].level = LogLevel::DEBUG;
        batch[5].level = LogLevel::DEBUG;

        filter.write(batch, 6);
    }

    std::ifstream file(filepath, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    TelemetryEvent read_events[4]{};
    file.read(reinterpret_cast<char*>(read_events), sizeof(read_events));
    EXPECT_EQ(file.gcount(), static_cast<std::streamsize>(4 * sizeof(TelemetryEvent)));

    EXPECT_EQ(read_events[0].sequence_num, 4001);
    EXPECT_EQ(read_events[1].sequence_num, 4002);
    EXPECT_EQ(read_events[2].sequence_num, 4004);
    EXPECT_EQ(read_events[3].sequence_num, 4005);

    file.close();
    std::remove(filepath.c_str());
}

} // namespace telemetry::testing
