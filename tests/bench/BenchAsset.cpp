#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Harness.hpp"
#include "../AssetFixtures.hpp"

import Engine.asset;
import Engine.mesh;
import Engine.Jobs;

namespace bench {

namespace {

namespace fs = std::filesystem;

constexpr int kLargeRings = 300;
constexpr int kLargeSegments = 300;
constexpr int kBatchCount = 8;
constexpr int kBatchRings = 120;
constexpr int kBatchSegments = 120;
constexpr const char* kFixtureRoot = "/tmp/lit_bench_assets";

struct Fixture {
    std::vector<std::string> large;
    std::vector<std::string> batch;
    CacheOptions options;
    std::unique_ptr<JobSystem> jobs;
};

Fixture& fixture() {
    static Fixture shared = [] {
        Fixture built;
        const std::string root = kFixtureRoot;
        fs::create_directories(root);
        built.large = assetfixtures::writeObjSet(root + "/large", 1, kLargeRings, kLargeSegments);
        built.batch = assetfixtures::writeObjSet(root + "/batch", kBatchCount, kBatchRings, kBatchSegments);
        built.options.directory = root + "/cache";
        built.jobs = std::make_unique<JobSystem>();
        return built;
    }();
    return shared;
}

void clearCache() {
    std::error_code error;
    fs::remove_all(fixture().options.directory, error);
}

double trianglesIn(const std::vector<std::string>& paths) {
    const int perFile = paths.size() == 1 ? kLargeRings * kLargeSegments * 2 : kBatchRings * kBatchSegments * 2;
    return static_cast<double>(perFile) * static_cast<double>(paths.size());
}

Case singleCase(bool warm) {
    Fixture& shared = fixture();
    Case benchCase;
    benchCase.itemsPerIteration = trianglesIn(shared.large);
    benchCase.iterations = 5;
    benchCase.warmup = 1;
    benchCase.prepare = [warm] {
        clearCache();
        if (warm) AssetManager::loadProcessed(fixture().large[0], fixture().options);
    };
    benchCase.run = [] {
        const auto result = AssetManager::loadProcessed(fixture().large[0], fixture().options);
        sink(result ? static_cast<double>(result->base.indices.size()) : 0.0);
    };
    return benchCase;
}

Case batchCase(bool parallel, bool warm) {
    Fixture& shared = fixture();
    Case benchCase;
    benchCase.itemsPerIteration = trianglesIn(shared.batch);
    benchCase.iterations = 5;
    benchCase.warmup = 1;
    benchCase.prepare = [warm] {
        clearCache();
        if (warm) AssetManager::loadProcessedMany(fixture().batch, fixture().options, *fixture().jobs);
    };
    benchCase.run = [parallel] {
        static JobSystem serialJobs(0);
        JobSystem& jobs = parallel ? *fixture().jobs : serialJobs;
        const auto results = AssetManager::loadProcessedMany(fixture().batch, fixture().options, jobs);
        sink(static_cast<double>(results.size()));
    };
    return benchCase;
}

}

void registerAssetBenches() {
    add("asset.single_cold_180k_tris", [] { return singleCase(false); });
    add("asset.single_warm_180k_tris", [] { return singleCase(true); });
    add("asset.batch8_serial_cold", [] { return batchCase(false, false); });
    add("asset.batch8_parallel_cold", [] { return batchCase(true, false); });
    add("asset.batch8_serial_warm", [] { return batchCase(false, true); });
    add("asset.batch8_parallel_warm", [] { return batchCase(true, true); });
}

}
