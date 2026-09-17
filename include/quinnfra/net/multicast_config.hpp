#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace quinnfra::net {

struct MulticastConfig {
    std::string multicast_group;                       // Multicast IPv4 address (e.g. "239.255.0.1")
    uint16_t    port{12345};                           // UDP destination port
    std::string interface_ip{"0.0.0.0"};               // Local interface IP to join on ("0.0.0.0" for default)
    size_t      receive_buffer_size{4 * 1024 * 1024};  // SO_RCVBUF in bytes (4MB default)
    bool        loopback{true};                        // Enable local multicast loopback (IP_MULTICAST_LOOP)
    bool        reuse_address{true};                   // Enable SO_REUSEADDR
    bool        reuse_port{true};                      // Enable SO_REUSEPORT (if supported)

    bool operator==(const MulticastConfig&) const = default;
};

} // namespace quinnfra::net
