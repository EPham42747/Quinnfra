#pragma once
#include <fstream>
#include <string>
#include <string_view>

#include <quinnfra/telemetry/event.hpp>
#include <quinnfra/telemetry/format.hpp>
#include <quinnfra/telemetry/sinks/sink.hpp>

namespace telemetry {

/// @brief Formats TelemetryEvents into human-readable text logs.
class TextFileSink : public Sink {
public:
    /// @brief Opens or creates a text telemetry log file.
    /// @param filepath Path to the output log file.
    /// @param append If true, appends to existing file; if false, overwrites/truncates.
    explicit TextFileSink(std::string_view filepath, bool append = true)
        : stream_(filepath.data(), (append ? std::ios::app : std::ios::out)) {}

    ~TextFileSink() override {
        flush();
    }

    // Delete copy semantics
    // Prevent multiple sinks writing to same file and writing to a closed file
    TextFileSink(const TextFileSink&) = delete;
    TextFileSink& operator=(const TextFileSink&) = delete;

    TextFileSink(TextFileSink&&) noexcept = default;
    TextFileSink& operator=(TextFileSink&&) noexcept = default;

    /// @brief Returns true if the underlying file stream is open and ready for writing.
    [[nodiscard]] bool is_open() const noexcept {
        return stream_.is_open();
    }

    /// @brief Formats and writes a single telemetry event as a human-readable text line.
    void write(const TelemetryEvent& event) override {
        if (stream_.is_open()) {
            stream_ << "[" << event.timestamp_ns << " ns] "
                    << "[" << event.level << "] "
                    << "[SRC:" << event.source_id << "] "
                    << "[SEQ:" << event.sequence_num << "] "
                    << "[" << event.type << "]\n";
        }
    }

    /// @brief Flushes buffered writes to disk.
    void flush() override {
        if (stream_.is_open()) {
            stream_.flush();
        }
    }

private:
    std::ofstream stream_;
};

} // namespace telemetry
