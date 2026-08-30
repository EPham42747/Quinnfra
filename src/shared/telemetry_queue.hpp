#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "telemetry_event.hpp"

namespace telemetry {

/// @brief Compile-time constant for hardware cache line alignment.
#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
#else
constexpr size_t hardware_destructive_interference_size = 64;
#endif

namespace spsc {

/// @brief Cacheline-isolated shared memory layout for a lock-free SPSC ring buffer.
/// @tparam T Trivially copyable type.
/// @tparam Capacity Buffer size (must be a power of two).
template <typename T, size_t Capacity>
struct RingBufferLayout {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable<T>::value, "Queue elements must be trivially copyable");

    // Block 1: hot, producer-owned index
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> write_index{0};

    // Block 2: hot, consumer-owned index
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> read_index{0};

    // Block 3+: buffer
    alignas(hardware_destructive_interference_size) T buffer[Capacity];
};

/// @brief Producer view of the lock-free shared memory queue.
template <typename T, size_t Capacity>
class ProducerView {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BUFFER_MASK = Capacity - 1;

    /// @brief Constructs a new producer view.
    /// @param layout The shared memory layout to use.
    explicit ProducerView(RingBufferLayout<T, Capacity>* layout) noexcept
        : layout_{layout},
          cached_read_index_{layout->read_index.load(std::memory_order_relaxed)} {}

    /// @brief Attempts to push a single event onto the shared ring buffer.
    /// @param item Trivially copyable item to write.
    /// @return true if pushed successfully, false if the buffer is full.
    [[nodiscard]] inline bool try_push(const T& item) noexcept {
        const uint64_t current_write = layout_->write_index.load(std::memory_order_relaxed);

        // Check if queue is full using cached read index
        if (current_write - cached_read_index_ >= Capacity) {
            // Check again with actual read index
            cached_read_index_ = layout_->read_index.load(std::memory_order_acquire);
            if (current_write - cached_read_index_ >= Capacity) {
                // Queue is actually full
                return false;
            }
        }

        // Write to the pre-allocated slot
        layout_->buffer[current_write & BUFFER_MASK] = item;

        // Release barrier: makes payload bytes visible before incrementing write_index
        layout_->write_index.store(current_write + 1, std::memory_order_release);
        return true;
    }

private:
    RingBufferLayout<T, Capacity>* layout_;
    uint64_t cached_read_index_{0};
};

/// @brief Consumer view of the lock-free shared memory queue.
template <typename T, size_t Capacity>
class ConsumerView {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BUFFER_MASK = Capacity - 1;

    /// @brief Constructs a new consumer view.
    /// @param layout The shared memory layout to use.
    explicit ConsumerView(RingBufferLayout<T, Capacity>* layout) noexcept
        : layout_(layout),
          cached_write_index_(layout->write_index.load(std::memory_order_relaxed)) {}

    /// @brief Checks if an item is ready to read and returns a pointer to it.
    /// @return Pointer to the next item, or nullptr if queue is empty.
    [[nodiscard]] inline const T* front() noexcept {
        const uint64_t current_read = layout_->read_index.load(std::memory_order_relaxed);

        // Check if queue is empty using cached write index
        if (current_read >= cached_write_index_) {
            // Check again with actual write index
            cached_write_index_ = layout_->write_index.load(std::memory_order_acquire);
            if (current_read == cached_write_index_) {
                // Queue is actually empty
                return nullptr;
            }
        }

        return &layout_->buffer[current_read & BUFFER_MASK];
    }

    /// @brief Pops the front item off the queue, advancing the read index.
    inline void pop() noexcept {
        const uint64_t current_read = layout_->read_index.load(std::memory_order_relaxed);
        layout_->read_index.store(current_read + 1, std::memory_order_release);
    }

private:
    RingBufferLayout<T, Capacity>* layout_;
    uint64_t cached_write_index_{0};
};

} // namespace spsc


/// @brief Default capacity (~4MB), sized to avoid drops if the consumer briefly pauses.
constexpr size_t DEFAULT_QUEUE_CAPACITY = 65536;

/// @brief Shared memory layout type for telemetry.
using TelemetryQueue = spsc::RingBufferLayout<TelemetryEvent, DEFAULT_QUEUE_CAPACITY>;

/// @brief Producer type for emitting telemetry.
using TelemetryProducer = spsc::ProducerView<TelemetryEvent, DEFAULT_QUEUE_CAPACITY>;

/// @brief Consumer type for reading telemetry.
using TelemetryConsumer = spsc::ConsumerView<TelemetryEvent, DEFAULT_QUEUE_CAPACITY>;

} // namespace telemetry
