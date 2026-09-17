#pragma once
#include <quinnfra/ipc/spsc_consumer.hpp>

namespace quinnfra::telemetry {

/// @brief Consumer alias template for reading telemetry.
template <typename T, size_t Capacity = quinnfra::ipc::DEFAULT_QUEUE_CAPACITY>
using TelemetryConsumer = quinnfra::ipc::SpscConsumer<T, Capacity>;

} // namespace quinnfra::telemetry
