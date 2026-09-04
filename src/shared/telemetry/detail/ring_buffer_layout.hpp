#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace telemetry {

/// @brief Compile-time constant for hardware cache line alignment.
#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
#else
constexpr size_t hardware_destructive_interference_size = 64;
#endif

/// @brief Default capacity (~4MB), sized to avoid drops if the consumer briefly pauses.
constexpr size_t DEFAULT_QUEUE_CAPACITY = 65536;

namespace detail {

using telemetry::hardware_destructive_interference_size;

/// @brief Cacheline-isolated shared memory layout for a lock-free SPSC ring buffer.
/// @tparam T Trivially copyable type.
/// @tparam Capacity Buffer size (must be a power of two).
template <typename T, size_t Capacity>
struct RingBufferLayout {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable<T>::value, "Queue elements must be trivially copyable");

    // Cache line 0: producer-owned data
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> write_index{0};
    std::atomic<uint64_t> dropped_events{0};

    // Cache line 1: consumer-owned data
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> read_index{0};

    // Cache line 2+: buffer
    alignas(hardware_destructive_interference_size) T buffer[Capacity];
};

} // namespace detail

using detail::RingBufferLayout;

} // namespace telemetry
