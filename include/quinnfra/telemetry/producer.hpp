#pragma once
#include <atomic>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "detail/ring_buffer_mapping.hpp"
#include "event.hpp"

namespace telemetry {

/// @brief Producer view of the lock-free shared memory queue.
template <typename T, size_t Capacity>
class ProducerView {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BUFFER_MASK = Capacity - 1;
    using Layout = detail::RingBufferLayout<T, Capacity>;

    explicit ProducerView(Layout* layout) noexcept
        : layout_{layout},
          cached_read_index_{layout ? layout->read_index.load(std::memory_order_relaxed) : 0} {}

    /// @brief Creates an RAII-managed producer that owns and initializes the queue.
    static std::optional<ProducerView<T, Capacity>> create(std::string_view shm_name) {
        auto mapping = detail::RingBufferMapping<Layout>::create(shm_name);
        return mapping ? std::optional<ProducerView<T, Capacity>>(ProducerView(std::move(*mapping)))
                       : std::nullopt;
    }

    /// @brief Attempts to push a single event onto the shared queue.
    /// @param item Trivially copyable item to write.
    /// @return true if pushed successfully, false if the queue is full.
    [[nodiscard]] inline bool try_push(const T& item) noexcept {
        const uint64_t current_write = layout_->write_index.load(std::memory_order_relaxed);

        // Check if queue is full using cached read index
        if (current_write - cached_read_index_ >= Capacity) {
            // Check again with actual read index
            cached_read_index_ = layout_->read_index.load(std::memory_order_acquire);
            if (current_write - cached_read_index_ >= Capacity) {
                // Queue is full
                layout_->dropped_events.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        // Write to pre-allocated slot and update write index
        layout_->buffer[current_write & BUFFER_MASK] = item;
        layout_->write_index.store(current_write + 1, std::memory_order_release);
        return true;
    }

private:
    explicit ProducerView(detail::RingBufferMapping<Layout>&& mapping) noexcept
        : layout_{&mapping.layout()},
          cached_read_index_{0},
          shm_mapping_{std::move(mapping)} {}

    Layout* layout_{nullptr};
    uint64_t cached_read_index_{0};
    std::optional<detail::RingBufferMapping<Layout>> shm_mapping_{std::nullopt};
};

/// @brief Producer type for emitting telemetry.
using TelemetryProducer = ProducerView<TelemetryEvent, DEFAULT_QUEUE_CAPACITY>;

} // namespace telemetry
