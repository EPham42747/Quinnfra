cmake_minimum_required(VERSION 3.20)
project(quinnfra CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable testing
enable_testing()
find_package(Threads REQUIRED)

# Enable gtest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # For Windows MSVC runtime compatibility
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# Compile unit tests
add_executable(unit_tests
    tests/telemetry/test_queue.cpp
    tests/telemetry/test_sinks.cpp
)
target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(unit_tests PRIVATE
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(unit_tests)

# Enable googlebenchmark
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Compile benchmarks
add_executable(benchmarks
    benchmarks/telemetry/bench_queue.cpp
)
target_include_directories(benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(benchmarks PRIVATE
    benchmark::benchmark_main
    Threads::Threads
)

# Compile example telemetry daemon
add_executable(telemetry_daemon
    examples/telemetry/daemon.cpp
)
target_include_directories(telemetry_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/telemetry
)
target_link_libraries(telemetry_daemon PRIVATE
    Threads::Threads
)

# Compile example telemetry mock producer for daemon
add_executable(telemetry_mock_producer
    examples/telemetry/mock_producer.cpp
)
target_include_directories(telemetry_mock_producer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/telemetry
)
target_link_libraries(telemetry_mock_producer PRIVATE
    Threads::Threads
)
