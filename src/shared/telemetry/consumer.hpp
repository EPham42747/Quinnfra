#pragma once
#include <atomic>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "detail/ring_buffer_mapping.hpp"
#include "telemetry_event.hpp"

namespace telemetry {

/// @brief Consumer view of the lock-free shared memory queue.
template <typename T, size_t Capacity>
class ConsumerView {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BUFFER_MASK = Capacity - 1;
    using Layout = detail::RingBufferLayout<T, Capacity>;

    explicit ConsumerView(Layout* layout) noexcept
        : layout_{layout},
          cached_write_index_{layout ? layout->write_index.load(std::memory_order_relaxed) : 0} {}

    /// @brief Attaches an RAII-managed consumer to an existing queue.
    static std::optional<ConsumerView<T, Capacity>> attach(std::string_view shm_name) {
        auto mapping = detail::RingBufferMapping<Layout>::attach(shm_name);
        if (!mapping.has_value()) {
            return std::nullopt;
        }
        
        return mapping ? std::optional<ConsumerView<T, Capacity>>(ConsumerView(std::move(*mapping)))
                       : std::nullopt;
    }

    /// @brief Returns a pointer to the front element, or nullptr if the queue is empty.
    [[nodiscard]] inline const T* front() noexcept {
        const uint64_t current_read = layout_->read_index.load(std::memory_order_relaxed);

        if (current_read >= cached_write_index_) {
            cached_write_index_ = layout_->write_index.load(std::memory_order_acquire);
            if (current_read == cached_write_index_) {
                return nullptr;
            }
        }

        return &layout_->buffer[current_read & BUFFER_MASK];
    }

    /// @brief Pops the front element from the queue. Must only be called if front() returned non-null.
    inline void pop() noexcept {
        const uint64_t current_read = layout_->read_index.load(std::memory_order_relaxed);
        layout_->read_index.store(current_read + 1, std::memory_order_release);
    }

    /// @brief Returns the total count of dropped events reported by the producer.
    [[nodiscard]] inline uint64_t dropped_count() const noexcept {
        return layout_ ? layout_->dropped_events.load(std::memory_order_relaxed) : 0;
    }

private:
    explicit ConsumerView(detail::RingBufferMapping<Layout>&& mapping) noexcept
        : layout_{&mapping.layout()},
          cached_write_index_{0},
          shm_mapping_{std::move(mapping)} {}
    
    Layout* layout_{nullptr};
    uint64_t cached_write_index_{0};
    std::optional<detail::RingBufferMapping<Layout>> shm_mapping_{std::nullopt};
};

/// @brief Consumer type for reading telemetry.
using TelemetryConsumer = ConsumerView<TelemetryEvent, DEFAULT_QUEUE_CAPACITY>;

} // namespace telemetry
