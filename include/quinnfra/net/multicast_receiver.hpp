#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <quinnfra/net/multicast_config.hpp>
#include <quinnfra/net/packet_receiver.hpp>

namespace quinnfra::net {

/**
 * @brief Zero-allocation, non-blocking POSIX UDP multicast receiver.
 *
 * Implements the PacketReceiver concept. Manages socket lifecycle, multicast group membership,
 * and non-blocking packet polling.
 */
class PosixMulticastReceiver {
public:
    ~PosixMulticastReceiver() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Delete copy semantics
    PosixMulticastReceiver(const PosixMulticastReceiver&) = delete;
    PosixMulticastReceiver& operator=(const PosixMulticastReceiver&) = delete;

    // Move constructor
    PosixMulticastReceiver(PosixMulticastReceiver&& other) noexcept
        : fd_{other.fd_} {
        other.fd_ = -1;
    }

    // Move assignment overload
    PosixMulticastReceiver& operator=(PosixMulticastReceiver&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    /// @brief Factory creating and initializing a POSIX multicast receiver socket.
    /// @param config Multicast network configuration.
    /// @return Initialized receiver if successful, std::nullopt otherwise.
    [[nodiscard]] static std::optional<PosixMulticastReceiver> create(const MulticastConfig& config) {
        // Convert multicast group IP to binary
        in_addr group_addr{};
        if (::inet_pton(AF_INET, config.multicast_group.c_str(), &group_addr) != 1) {
            return std::nullopt;
        }

        // Convert interface IP to binary or default
        in_addr interface_addr{};
        if (config.interface_ip.empty() || config.interface_ip == "0.0.0.0") {
            interface_addr.s_addr = htonl(INADDR_ANY);
        }
        else if (::inet_pton(AF_INET, config.interface_ip.c_str(), &interface_addr) != 1) {
            return std::nullopt;
        }

        // Create UDP socket
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return std::nullopt;
        }

        // Set non-blocking mode
        int flags = ::fcntl(fd, F_GETFL, 0);  // Get flags
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {  // Add non-blocking flag
            ::close(fd);
            return std::nullopt;
        }

        // Set address reuse (local addresses and ports can be reused)
        if (config.reuse_address) {
            int optval = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
                ::close(fd);
                return std::nullopt;
            }
        }

        // Set port reuse, if supported (socket bindings can be reused)
#ifdef SO_REUSEPORT
        if (config.reuse_port) {
            int optval = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
                ::close(fd);
                return std::nullopt;
            }
        }
#endif

        // Set receive buffer size
        if (config.receive_buffer_size > 0) {
            int optval = static_cast<int>(config.receive_buffer_size);

            // Best-effort since some hardware can limit buffer size
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
        }

        // Set loopback (routing multicast messages from localhost back to localhost)
        if (config.loopback) {
            int optval = 1;
            if (::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &optval, sizeof(optval))) {
                ::close(fd);
                return std::nullopt;
            }
        }

        // Bind socket to destination port
        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(config.port);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Multicast IP isn't a bindable interface

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            ::close(fd);
            return std::nullopt;
        }

        // Join multicast group on specified interface
        ip_mreq mreq{};
        mreq.imr_multiaddr = group_addr;
        mreq.imr_interface = interface_addr;

        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            ::close(fd);
            return std::nullopt;
        }
        return PosixMulticastReceiver(fd);
    }

    /// @brief Polls for an incoming datagram into a caller-supplied buffer without blocking.
    /// @param buffer Pre-allocated destination buffer to receive packet bytes.
    /// @return Number of bytes received if a packet was available, std::nullopt otherwise.
    [[nodiscard]] std::optional<size_t> receive(std::span<uint8_t> buffer) noexcept {
        if (fd_ < 0 || buffer.empty()) {
            return std::nullopt;
        }

        ssize_t bytes_received = ::recv(fd_, buffer.data(), buffer.size(), 0);
        if (bytes_received < 0 || buffer.empty()) {
            return std::nullopt;
        }

        return static_cast<size_t>(bytes_received);
    }

    [[nodiscard]] int native_handle() const noexcept {
        return fd_;
    }

private:
    explicit PosixMulticastReceiver(int fd) noexcept
        : fd_{fd} {}

    int fd_{-1};
};

static_assert(PacketReceiver<PosixMulticastReceiver>, "PosixMulticastReceiver must satisfy PacketReceiver");

} // namespace quinnfra::net
