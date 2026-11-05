#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>

#include "Harness.hpp"

namespace bench {

namespace {

constexpr double kP95Fraction = 0.95;
constexpr double kMillisPerSecond = 1000.0;

double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const size_t index = static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(index, sorted.size() - 1)];
}

}

std::vector<Registration>& registry() {
    static std::vector<Registration> entries;
    return entries;
}

void add(std::string name, Factory build) { registry().push_back({std::move(name), std::move(build)}); }

Result measure(const std::string& name, const Case& benchCase) {
    const auto prepare = [&] {
        if (benchCase.prepare) benchCase.prepare();
    };
    for (int round = 0; round < benchCase.warmup; ++round) {
        prepare();
        benchCase.run();
    }
    std::vector<double> millis;
    millis.reserve(static_cast<size_t>(benchCase.iterations));
    uint64_t allocations = 0;
    uint64_t bytes = 0;
    for (int round = 0; round < benchCase.iterations; ++round) {
        prepare();
        const uint64_t allocationsBefore = allocationCount();
        const uint64_t bytesBefore = allocatedBytes();
        const auto start = std::chrono::steady_clock::now();
        benchCase.run();
        const auto stop = std::chrono::steady_clock::now();
        allocations += allocationCount() - allocationsBefore;
        bytes += allocatedBytes() - bytesBefore;
        millis.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(millis.begin(), millis.end());
    Result result;
    result.name = name;
    result.iterations = benchCase.iterations;
    result.itemsPerIteration = benchCase.itemsPerIteration;
    result.medianMs = percentile(millis, 0.5);
    result.p95Ms = percentile(millis, kP95Fraction);
    result.minMs = millis.empty() ? 0.0 : millis.front();
    result.meanMs = millis.empty() ? 0.0 : std::accumulate(millis.begin(), millis.end(), 0.0) / static_cast<double>(millis.size());
    result.itemsPerSecond = result.medianMs > 0.0 ? benchCase.itemsPerIteration * kMillisPerSecond / result.medianMs : 0.0;
    result.allocsPerIteration = static_cast<double>(allocations) / std::max(1, benchCase.iterations);
    result.bytesPerIteration = static_cast<double>(bytes) / std::max(1, benchCase.iterations);
    return result;
}

void printTable(const std::vector<Result>& results) {
    std::printf("%-34s %10s %10s %14s %12s %14s\n", "benchmark", "median_ms", "p95_ms", "items/sec", "allocs/iter", "bytes/iter");
    for (const Result& result : results) {
        std::printf("%-34s %10.4f %10.4f %14.0f %12.1f %14.0f\n", result.name.c_str(), result.medianMs, result.p95Ms, result.itemsPerSecond, result.allocsPerIteration, result.bytesPerIteration);
    }
}

}
