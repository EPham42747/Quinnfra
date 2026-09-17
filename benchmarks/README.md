# Benchmarks

Microbenchmarks evaluating the lock-free, Single-Producer-Single-Consumer (SPSC) ring buffer powering Quinnfra. Quantifies hot-path producer write overhead, consumer peek/pop latency, and sustained multi-threaded IPC throughput.


### Test Environment

* **Hardware:** Apple Silicon (10 Cores: L1D 64 KiB, L1I 128 KiB, L2 6144 KiB)
* **Compiler:** Apple Clang (`-std=c++20`, `-O3` Release)
* **Framework:** Google Benchmark v1.8.3
* **Event Size:** 64 bytes (`alignas(64)`)
* **Sampling:** 10 trials per benchmark

### Reproduction

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target benchmarks

./build/benchmarks \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true \
  --benchmark_time_unit=ns
```


## Results

### IPC SPSC Queue

| Benchmark Scenario | Median Latency | Mean Latency | StdDev (CV%) | Median Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Producer `try_push()`** | **1.79 ns** | 1.78 ns | 0.011 ns (0.62%) | **561.0 M/s** |
| **Consumer `front()` + `pop()`** | **1.73 ns** | 1.73 ns | 0.015 ns (0.85%) | **579.7 M/s** |
| **Single-Thread Round-Trip** | **2.84 ns** | 2.82 ns | 0.019 ns (0.66%) | **355.2 M/s** |
| **Concurrent SPSC** (2 Threads) | **2.66 ns** | 2.63 ns | 0.092 ns (3.46%) | **191.0 M/s** (~12.2 GB/s) | **** |
