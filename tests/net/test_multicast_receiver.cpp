#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <quinnfra/net/multicast_config.hpp>
#include <quinnfra/net/multicast_receiver.hpp>
#include <quinnfra/net/packet_receiver.hpp>

namespace quinnfra::net::testing {
    
namespace {

// Validate MulticastReceiver
static_assert(PacketReceiver<MulticastReceiver>, "MulticastReceiver must satisfy PacketReceiver concept");

struct MockCustomReceiver {
    std::optional<size_t> receive(std::span<uint8_t> buffer) {
        if (buffer.empty()) {
            return std::nullopt;
        }
        buffer[0] = 1;
        return 1;
    }
};
static_assert(PacketReceiver<MockCustomReceiver>, "MockCustomReceiver must satisfy PacketReceiver concept");

bool send_udp_multicast(const std::string& group, uint16_t port, const void* data, size_t len, const std::string& iface_ip = "127.0.0.1") {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    in_addr iface_addr{};
    if (::inet_pton(AF_INET, iface_ip.c_str(), &iface_addr) == 1) {
        ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface_addr, sizeof(iface_addr));
    }

    unsigned char loop = 1;
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    ::inet_pton(AF_INET, group.c_str(), &dest_addr.sin_addr);

    ssize_t sent = ::sendto(fd, data, len, 0, reinterpret_cast<const sockaddr*>(&dest_addr), sizeof(dest_addr));
    ::close(fd);
    return sent == static_cast<ssize_t>(len);
}

} // namespace

TEST(MulticastReceiverTest, FactoryValidatesInvalidIpAddresses) {
    MulticastConfig bad_group{};
    bad_group.multicast_group = "999.999.999.999";
    bad_group.port = 30001;
    EXPECT_FALSE(MulticastReceiver::create(bad_group).has_value());

    MulticastConfig bad_iface{};
    bad_iface.multicast_group = "239.255.0.1";
    bad_iface.interface_ip = "invalid-interface-ip";
    bad_iface.port = 30002;
    EXPECT_FALSE(MulticastReceiver::create(bad_iface).has_value());
}

TEST(MulticastReceiverTest, NonBlockingEmptySocketReturnsNullopt) {
    MulticastConfig config{};
    config.multicast_group = "239.255.0.1";
    config.port = 30010;
    config.interface_ip = "127.0.0.1";

    auto receiver = MulticastReceiver::create(config);
    ASSERT_TRUE(receiver.has_value());

    std::array<uint8_t, 1500> buffer{};
    auto result = receiver->receive(buffer);
    EXPECT_FALSE(result.has_value());
}

TEST(MulticastReceiverTest, LoopbackSendAndReceiveExactPayload) {
    MulticastConfig config{};
    config.multicast_group = "239.255.0.2";
    config.port = 30020;
    config.interface_ip = "127.0.0.1";

    auto receiver = MulticastReceiver::create(config);
    ASSERT_TRUE(receiver.has_value());

    const std::string payload = "SAMPLE_PAYLOAD";
    ASSERT_TRUE(send_udp_multicast(config.multicast_group, config.port, payload.data(), payload.size(), config.interface_ip));

    // Poll until packet arrives with timeout
    std::array<uint8_t, 1500> buffer{};
    std::optional<size_t> bytes_received;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(500);  // Arbitrary wait

    while (std::chrono::steady_clock::now() < deadline) {
        bytes_received = receiver->receive(buffer);
        if (bytes_received.has_value()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    ASSERT_TRUE(bytes_received.has_value());
    EXPECT_EQ(*bytes_received, payload.size());
    std::string received_str(reinterpret_cast<const char*>(buffer.data()), *bytes_received);
    EXPECT_EQ(received_str, payload);
}

TEST(MulticastReceiverTest, MoveSemanticsTransferDescriptor) {
    MulticastConfig config{};
    config.multicast_group = "239.255.0.4";
    config.port = 30040;
    config.interface_ip = "127.0.0.1";

    auto receiver1 = MulticastReceiver::create(config);
    ASSERT_TRUE(receiver1.has_value());
    int original_fd = receiver1->native_handle();
    EXPECT_GE(original_fd, 0);

    // Move construct
    MulticastReceiver receiver2(std::move(*receiver1));
    EXPECT_EQ(receiver1->native_handle(), -1);
    EXPECT_EQ(receiver2.native_handle(), original_fd);

    // Move assign
    MulticastReceiver receiver3 = std::move(receiver2);
    EXPECT_EQ(receiver2.native_handle(), -1);
    EXPECT_EQ(receiver3.native_handle(), original_fd);
}

} // namespace quinnfra::net::testing
