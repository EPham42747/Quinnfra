#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include <quinnfra/telemetry/event.hpp>
#include <quinnfra/telemetry/sinks/sink.hpp>

namespace telemetry {

/// @brief Decorator that wraps an underlying Sink and filters events.
class FilteredSink : public Sink {
public:
    using FilterRule = std::function<bool(const TelemetryEvent&)>;

    /// @brief Constructs a FilteredSink with a custom filter rule.
    /// @param rule Callable returning true if the event should be forwarded, false to discard.
    /// @param inner_sink The downstream sink that receives events satisfying the rule.
    explicit FilteredSink(FilterRule rule, std::unique_ptr<Sink> inner_sink)
        : rule_{std::move(rule)}, inner_sink_{std::move(inner_sink)} {}

    ~FilteredSink() override = default;

    // Delete copy semantics
    // Prevent multiple filters feeding to the same sink
    FilteredSink(const FilteredSink&) = delete;
    FilteredSink& operator=(const FilteredSink&) = delete;

    FilteredSink(FilteredSink&&) noexcept = default;
    FilteredSink& operator=(FilteredSink&&) noexcept = default;

    /// @brief Forwards the event to the inner sink if it satisfies the filter rule.
    void write(const TelemetryEvent& event) override {
        if (inner_sink_ && (!rule_ || rule_(event))) {
            inner_sink_->write(event);
        }
    }

    /// @brief Filters and forwards contiguous slices of events satisfying the filter rule.
    void write(const TelemetryEvent* events, size_t count) override {
        if (!inner_sink_ || events == nullptr || count == 0) {
            return;
        }

        size_t run_start = 0;
        size_t run_length = 0;

        for (size_t i = 0; i < count; ++i) {
            if (!rule_ || rule_(events[i])) {
                if (run_length == 0) {
                    run_start = i;
                }
                ++run_length;
            } else if (run_length > 0) {
                inner_sink_->write(&events[run_start], run_length);
                run_length = 0;
            }
        }

        if (run_length > 0) {
            inner_sink_->write(&events[run_start], run_length);
        }
    }

    /// @brief Flushes the wrapped downstream sink.
    void flush() override {
        if (inner_sink_) {
            inner_sink_->flush();
        }
    }

private:
    FilterRule rule_;
    std::unique_ptr<Sink> inner_sink_;
};

} // namespace telemetry
