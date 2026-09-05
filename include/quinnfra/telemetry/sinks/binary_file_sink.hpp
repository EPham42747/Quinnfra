#pragma once
#include <fstream>
#include <string>
#include <string_view>

#include <quinnfra/telemetry/sinks/sink.hpp>

namespace telemetry {

/// @brief High-performance sink that dumps raw 64-byte TelemetryEvent structs to disk.
class BinaryFileSink : public Sink {
public:
    /// @brief Opens or creates a binary telemetry log file.
    /// @param filepath Path to the output binary file.
    /// @param append If true, appends to existing file; if false, overwrites/truncates.
    explicit BinaryFileSink(std::string_view filepath, bool append = true)
        : stream_(filepath.data(), std::ios::binary | (append ? std::ios::app : std::ios::out)) {}

    ~BinaryFileSink() override {
        flush();
    }

    // Delete copy semantics
    // Prevent multiple sinks writing to same file and writing to a closed file
    BinaryFileSink(const BinaryFileSink&) = delete;
    BinaryFileSink& operator=(const BinaryFileSink&) = delete;

    BinaryFileSink(BinaryFileSink&&) noexcept = default;
    BinaryFileSink& operator=(BinaryFileSink&&) noexcept = default;

    /// @brief Returns true if the underlying file stream is open and valid for writing.
    [[nodiscard]] bool is_open() const noexcept {
        return stream_.is_open();
    }

    /// @brief Writes a single 64-byte TelemetryEvent struct directly to the file.
    void write(const TelemetryEvent& event) override {
        if (stream_.is_open()) {
            stream_.write(reinterpret_cast<const char*>(&event), sizeof(TelemetryEvent));
        }
    }

    /// @brief High-throughput batch write: dumps contiguous array of events in a single I/O call.
    void write(const TelemetryEvent* events, size_t count) override {
        if (stream_.is_open() && events != nullptr && count > 0) {
            stream_.write(
                reinterpret_cast<const char*>(events),
                static_cast<std::streamsize>(sizeof(TelemetryEvent) * count)
            );
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

