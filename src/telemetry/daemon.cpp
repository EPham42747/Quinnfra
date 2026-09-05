#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#include <quinnfra/telemetry/consumer.hpp>
#include <quinnfra/telemetry/event.hpp>
#include <quinnfra/telemetry/sinks/binary_file_sink.hpp>
#include <quinnfra/telemetry/sinks/filtered_sink.hpp>
#include <quinnfra/telemetry/sinks/sink.hpp>
#include <quinnfra/telemetry/sinks/text_file_sink.hpp>

namespace {
std::atomic<bool> g_running{true};

void handle_shutdown_signal(int signum) noexcept {
    (void)signum;
    g_running.store(false, std::memory_order_relaxed);
}

struct DaemonConfig {
    std::string shm_name;
    std::optional<std::string> binary_log;
    std::optional<std::string> text_log;
    std::optional<std::string> error_log;
};

void print_usage(std::string_view prog_name) {
    std::cout << "Usage: " << prog_name << " [--shm] <shm_name> [options]\n\n"
              << "Required:\n"
              << "  --shm <name>          POSIX shared memory name (must start with '/')\n"
              << "                        (Or pass <shm_name> directly as a positional argument)\n\n"
              << "Options:\n"
              << "  --binary-log <file>   Output raw 64-byte binary events to <file>\n"
              << "  --text-log <file>     Output human-readable formatted events to <file>\n"
              << "  --error-log <file>    Output filtered WARN, ERROR, and FATAL events to <file>\n"
              << "  -h, --help            Show this help message\n";
}

std::optional<DaemonConfig> parse_arguments(int argc, char* argv[]) {
    DaemonConfig config{};

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return std::nullopt;
        }
        else if (arg == "--shm") {
            if (++i < argc) {
                config.shm_name = argv[i];
            }
            else {
                std::cerr << "Error: --shm requires an argument\n";
                return std::nullopt;
            }
        }
        else if (arg == "--binary-log") {
            if (++i < argc) {
                config.binary_log = argv[i];
            }
            else {
                std::cerr << "Error: --binary-log requires an argument\n";
                return std::nullopt;
            }
        }
        else if (arg == "--text-log") {
            if (++i < argc) {
                config.text_log = argv[i];
            }
            else {
                std::cerr << "Error: --text-log requires an argument\n";
                return std::nullopt;
            }
        }
        else if (arg == "--error-log") {
            if (++i < argc) {
                config.error_log = argv[i];
            }
            else {
                std::cerr << "Error: --error-log requires an argument\n";
                return std::nullopt;
            }
        }
        else if (!arg.empty() && arg[0] == '/') {
            if (config.shm_name.empty()) {
                config.shm_name = arg;
            }
            else {
                std::cerr << "Error: Duplicate or unexpected shared memory argument: " << arg << "\n\n";
                print_usage(argv[0]);
                return std::nullopt;
            }
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            print_usage(argv[0]);
            return std::nullopt;
        }
    }

    if (config.shm_name.empty()) {
        std::cerr << "Error: --shm <name> is required.\n\n";
        print_usage(argv[0]);
        return std::nullopt;
    }

    if (config.shm_name.front() != '/') {
        std::cerr << "Error: shm_name must start with '/'\n";
        return std::nullopt;
    }

    return config;
}
} // namespace

int main(int argc, char* argv[]) {
    // Parse CLI arguments
    auto config_opt = parse_arguments(argc, argv);
    if (!config_opt.has_value()) {
        return 1;
    }
    const auto& config = *config_opt;

    // Register signal handlers
    struct ::sigaction sa{};
    sa.sa_handler = handle_shutdown_signal;
    sigemptyset(&sa.sa_mask); // C macro on macOS/BSD
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    std::cout << "[Telemetry Daemon] Starting...\n"
              << "[Telemetry Daemon] Connecting to segment: " << config.shm_name << "\n";

    // Attach to shared memory queue
    constexpr size_t QUEUE_CAPACITY = telemetry::DEFAULT_QUEUE_CAPACITY;
    auto consumer_opt = telemetry::ConsumerView<telemetry::TelemetryEvent, QUEUE_CAPACITY>::attach(config.shm_name);

    if (!consumer_opt.has_value()) {
        std::cerr << "[Telemetry Daemon] FATAL: Failed to attach to shared memory segment: "
                  << config.shm_name << " (errno: " << errno << ")\n";
        return 1;
    }

    auto& consumer = *consumer_opt;
    std::cout << "[Telemetry Daemon] Attached successfully.\n";

    // Initialize sinks
    std::vector<std::unique_ptr<telemetry::Sink>> sinks;

    if (config.binary_log.has_value()) {
        auto bin_sink = std::make_unique<telemetry::BinaryFileSink>(*config.binary_log);
        if (bin_sink->is_open()) {
            std::cout << "[Telemetry Daemon] Registered Binary Sink: " << *config.binary_log << "\n";
            sinks.push_back(std::move(bin_sink));
        }
        else {
            std::cerr << "[Telemetry Daemon] WARNING: Could not open binary log: " << *config.binary_log << "\n";
        }
    }

    if (config.text_log.has_value()) {
        auto txt_sink = std::make_unique<telemetry::TextFileSink>(*config.text_log);
        if (txt_sink->is_open()) {
            std::cout << "[Telemetry Daemon] Registered Text Sink: " << *config.text_log << "\n";
            sinks.push_back(std::move(txt_sink));
        }
        else {
            std::cerr << "[Telemetry Daemon] WARNING: Could not open text log: " << *config.text_log << "\n";
        }
    }

    if (config.error_log.has_value()) {
        auto err_file = std::make_unique<telemetry::TextFileSink>(*config.error_log);
        if (err_file->is_open()) {
            auto err_sink = std::make_unique<telemetry::FilteredSink>(
                [](const telemetry::TelemetryEvent& ev) { return ev.level >= telemetry::LogLevel::WARN; },
                std::move(err_file)
            );
            std::cout << "[Telemetry Daemon] Registered Error Sink (WARN+): " << *config.error_log << "\n";
            sinks.push_back(std::move(err_sink));
        }
        else {
            std::cerr << "[Telemetry Daemon] WARNING: Could not open error log: " << *config.error_log << "\n";
        }
    }

    if (sinks.empty()) {
        std::cout << "[Telemetry Daemon] Notice: No log files configured. Running in memory-only drain mode.\n";
    }

    std::cout << "[Telemetry Daemon] Listening for events...\n";

    uint64_t total_events_processed = 0;
    uint32_t idle_spins = 0;

    // Drain loop
    while (g_running.load(std::memory_order_relaxed)) {
        if (const auto* event = consumer.front()) {
            for (auto& sink : sinks) {
                sink->write(*event);
            }

            consumer.pop();
            ++total_events_processed;
            idle_spins = 0;
        }
        else {
            // Progressive backoff
            if (idle_spins < 64) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#elif defined(__aarch64__) || defined(__arm64__)
                asm volatile("yield");
#endif
                ++idle_spins;
            }
            else if (idle_spins < 1024) {
                std::this_thread::yield(); // Yield OS timeslice
                ++idle_spins;
            }
            else {
                // Periodically flush sinks to disk during quiet periods
                for (auto& sink : sinks) {
                    sink->flush();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    }

    // Teardown
    std::cout << "\n[Telemetry Daemon] Shutdown signal received. Draining remaining queue items...\n";
    uint64_t drained_events = 0;

    while (const auto* event = consumer.front()) {
        for (auto& sink : sinks) {
            sink->write(*event);
        }
        consumer.pop();
        ++drained_events;
    }

    for (auto& sink : sinks) {
        sink->flush();
    }

    std::cout << "[Telemetry Daemon] Drain complete. Events drained: " << drained_events << "\n"
              << "[Telemetry Daemon] Total events recorded: " << (total_events_processed + drained_events) << "\n";

    // Consumer detaches from queue as it falls out of scope
    return 0;
}
