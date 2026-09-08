#pragma once
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <quinnfra/telemetry/sinks/sink.hpp>

namespace telemetry {

/// @brief Formats events into human-readable text logs using a provided formatter callable.
template <typename T>
class TextFileSink : public Sink<T> {
public:
    using Formatter = std::function<std::string(const T&)>;

    /// @brief Opens or creates a text telemetry log file.
    /// @param filepath Path to the output log file.
    /// @param formatter Callable converting an item of type T to a formatted string.
    /// @param append If true, appends to existing file; if false, overwrites/truncates.
    explicit TextFileSink(std::string_view filepath, Formatter formatter, bool append = true)
        : stream_(filepath.data(), (append ? std::ios::app : std::ios::out)),
          formatter_{std::move(formatter)} {}

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
    void write(const T& event) override {
        if (stream_.is_open() && formatter_) {
            stream_ << formatter_(event);
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
    Formatter formatter_;
};

} // namespace telemetry
