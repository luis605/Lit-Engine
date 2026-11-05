#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "Harness.hpp"

import Engine.Json;

namespace bench {

namespace {

constexpr double kMinimumRegressionMs = 0.02;
constexpr double kMinimumAllocationDelta = 1.0;
constexpr int kJsonIndent = 2;
constexpr int kFormatVersion = 1;

Json toJson(const Result& result) {
    Json entry = Json::object();
    entry.set("name", result.name);
    entry.set("iterations", result.iterations);
    entry.set("items_per_iteration", result.itemsPerIteration);
    entry.set("median_ms", result.medianMs);
    entry.set("p95_ms", result.p95Ms);
    entry.set("min_ms", result.minMs);
    entry.set("mean_ms", result.meanMs);
    entry.set("items_per_sec", result.itemsPerSecond);
    entry.set("allocs_per_iter", result.allocsPerIteration);
    entry.set("bytes_per_iter", result.bytesPerIteration);
    return entry;
}

double numberOf(const Json& entry, const char* key) {
    const Json* value = entry.get(key);
    return value ? value->asNumber() : 0.0;
}

bool regressed(double current, double baseline, double tolerance, double minimumDelta) {
    return current > baseline * (1.0 + tolerance) && current - baseline >= minimumDelta;
}

}

bool writeJson(const std::string& path, const std::vector<Result>& results) {
    Json list = Json::array();
    for (const Result& result : results) list.push(toJson(result));
    Json document = Json::object();
    document.set("version", kFormatVersion);
    document.set("benchmarks", std::move(list));
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << document.dump(kJsonIndent) << '\n';
    return static_cast<bool>(out);
}

int compareWithBaseline(const std::string& path, const std::vector<Result>& results, double tolerancePercent) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read baseline %s\n", path.c_str());
        return -1;
    }
    std::stringstream text;
    text << in.rdbuf();
    std::string error;
    const auto document = Json::parse(text.str(), &error);
    const Json* list = document ? document->get("benchmarks") : nullptr;
    if (!list) {
        std::fprintf(stderr, "bad baseline %s: %s\n", path.c_str(), error.c_str());
        return -1;
    }
    std::map<std::string, const Json*> baseline;
    for (const Json& entry : list->items()) {
        if (const Json* name = entry.get("name")) baseline[name->asString()] = &entry;
    }
    const double tolerance = tolerancePercent / 100.0;
    int regressions = 0;
    for (const Result& result : results) {
        const auto found = baseline.find(result.name);
        if (found == baseline.end()) {
            std::printf("NEW      %-34s (not in baseline)\n", result.name.c_str());
            continue;
        }
        const double baseMs = numberOf(*found->second, "median_ms");
        const double baseAllocs = numberOf(*found->second, "allocs_per_iter");
        const bool slower = regressed(result.medianMs, baseMs, tolerance, kMinimumRegressionMs);
        const bool allocMore = regressed(result.allocsPerIteration, baseAllocs, tolerance, kMinimumAllocationDelta);
        const double change = baseMs > 0.0 ? (result.medianMs / baseMs - 1.0) * 100.0 : 0.0;
        std::printf("%-8s %-34s %9.4f -> %9.4f ms (%+6.1f%%)  allocs %9.1f -> %9.1f\n", slower || allocMore ? "REGRESS" : "ok", result.name.c_str(), baseMs, result.medianMs, change, baseAllocs, result.allocsPerIteration);
        if (slower || allocMore) ++regressions;
    }
    return regressions;
}

}
