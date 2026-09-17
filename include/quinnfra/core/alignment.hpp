#pragma once
#include <cstddef>
#include <new>

namespace quinnfra::core {

/// @brief Compile-time constant for hardware cache line alignment.
#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
using std::hardware_constructive_interference_size;
#else
constexpr size_t hardware_destructive_interference_size = 64;
constexpr size_t hardware_constructive_interference_size = 64;
#endif

} // namespace quinnfra::core
