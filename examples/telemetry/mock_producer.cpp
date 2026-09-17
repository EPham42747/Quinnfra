#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

#include <quinnfra/telemetry/producer.hpp>
#include "event.hpp"

namespace {
std::atomic<bool> g_running{true};

void handle_shutdown_signal(int signum) noexcept {
    (void)signum;
    g_running.store(false, std::memory_order_relaxed);
}
} // namespace

int main(int argc, char* argv[]) {
    // Get or default shared memory name
    std::string_view shm_name = (argc > 1) ? argv[1] : "/telemetry";

    // Register signal handlers
    struct ::sigaction sa{};
    sa.sa_handler = handle_shutdown_signal;
    sigemptyset(&sa.sa_mask); // C macro on macOS/BSD
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    std::cout << "[Mock Producer] Creating shared memory segment: " << shm_name << "\n";

    auto producer_opt = quinnfra::telemetry::TelemetryProducer<examples::ExampleEvent>::create(shm_name);
    if (!producer_opt.has_value()) {
        std::cerr << "[Mock Producer] FATAL: Failed to create shared memory segment "
                  << shm_name << " (segment may already exist or permission denied).\n";
        return 1;
    }

    auto& producer = *producer_opt;
    std::cout << "[Mock Producer] Queue initialized. Streaming events every 500ms (Ctrl+C to stop)...\n";

    std::uint32_t seq = 1;
    const examples::LogLevel levels[] = {
        examples::LogLevel::INFO,
        examples::LogLevel::INFO,
        examples::LogLevel::WARN,
        examples::LogLevel::ERROR
    };

    while (g_running.load(std::memory_order_relaxed)) {
        examples::ExampleEvent ev{};
        ev.timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        ev.sequence_num = seq;
        ev.source_id = 0;
        ev.level = levels[seq % 4];
        ev.type = examples::EventType::HEARTBEAT;

        if (producer.try_push(ev)) {
            std::cout << "[Mock Producer] Pushed event #" << seq
                      << " [" << examples::name_of(ev.level) << "]\n";
            ++seq;
        }
        else {
            std::cerr << "[Mock Producer] Queue full! Dropped event #" << seq << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n[Mock Producer] Shutdown signal received. Exiting...\n"
              << "[Mock Producer] Shared memory segment unlinked via RAII.\n";
    return 0;
}
