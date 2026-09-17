#pragma once
#include <quinnfra/ipc/spsc_producer.hpp>

namespace quinnfra::telemetry {

/// @brief Producer alias template for emitting telemetry.
template <typename T, size_t Capacity = quinnfra::ipc::DEFAULT_QUEUE_CAPACITY>
using TelemetryProducer = quinnfra::ipc::SpscProducer<T, Capacity>;

} // namespace quinnfra::telemetry
