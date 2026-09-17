#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace quinnfra::mktdata {

/**
 * @brief Concept for zero-allocation packet receivers.
 *
 * Types modeling PacketReceiver ingest network packets into a caller-supplied buffer.
 * Polling should be non-blocking: returns the number of bytes read if a packet was
 * available and std::nullopt if not.
 */
template <typename T>
concept PacketReceiver = requires(T receiver, std::span<uint8_t> buffer) {
    { receiver.receive(buffer) } -> std::same_as<std::optional<size_t>>;
};

} // namespace quinnfra::mktdata
