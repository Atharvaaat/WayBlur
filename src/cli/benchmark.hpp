#ifndef WAYBLUR_BENCHMARK_HPP
#define WAYBLUR_BENCHMARK_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace wayblur {

struct BenchmarkReport {
    std::string resolution;
    int fps{60};
    double capture_latency_ms{0.0};
    double processing_latency_ms{0.0};
    uint64_t dropped_frames{0};
    std::string cpu_utilization;
    std::string gpu_utilization;
};

class Benchmark {
public:
    Benchmark();
    std::vector<BenchmarkReport> run_benchmark();
    void print_report(const std::vector<BenchmarkReport>& reports);
};

} // namespace wayblur

#endif // WAYBLUR_BENCHMARK_HPP
