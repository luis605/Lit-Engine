#include "TestSupport.hpp"
#include "AssetFixtures.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

import Engine.asset;
import Engine.mesh;
import Engine.Jobs;

namespace {

namespace fs = std::filesystem;

constexpr int kRings = 24;
constexpr int kSegments = 32;
constexpr size_t kCorruptOffset = 200;

struct Sandbox {
    std::string root;
    CacheOptions options;
    Sandbox() {
        root = testsupport::tempFilePath("litassetcache_", "dir");
        fs::remove_all(root);
        fs::create_directories(root);
        options.directory = root + "/cache";
    }
    ~Sandbox() { fs::remove_all(root); }
};

bool sameMesh(const Mesh& left, const Mesh& right) {
    return left.vertices == right.vertices && left.indices == right.indices;
}

bool sameProcessed(const ProcessedMesh& left, const ProcessedMesh& right) {
    if (!sameMesh(left.base, right.base) || left.lods.size() != right.lods.size()) return false;
    for (size_t level = 0; level < left.lods.size(); ++level) {
        if (!sameMesh(left.lods[level], right.lods[level])) return false;
    }
    return true;
}

void flipByte(const std::string& path, size_t offset) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(static_cast<std::streamoff>(offset));
    char byte = 0;
    file.get(byte);
    byte = static_cast<char>(byte ^ 0x5A);
    file.seekp(static_cast<std::streamoff>(offset));
    file.put(byte);
}

void testHitMissAndIdenticalResults() {
    Sandbox sandbox;
    const std::string source = sandbox.root + "/a.obj";
    assetfixtures::writeBumpySphereObj(source, kRings, kSegments, 0.0f);

    LoadReport firstReport;
    const auto cold = AssetManager::loadProcessed(source, sandbox.options, &firstReport);
    CHECK(cold.has_value());
    CHECK(firstReport.outcome == CacheOutcome::Miss);
    CHECK(fs::exists(firstReport.cachePath));
    CHECK(cold->lods.size() == kLodLevelCount - 1);

    LoadReport secondReport;
    const auto warm = AssetManager::loadProcessed(source, sandbox.options, &secondReport);
    CHECK(warm.has_value());
    CHECK(secondReport.outcome == CacheOutcome::Hit);
    CHECK(sameProcessed(*cold, *warm));

    const auto raw = AssetManager::importMesh(source);
    CHECK(raw.has_value());
    CHECK(sameProcessed(AssetManager::processMesh(*raw), *warm));

    CacheOptions disabled;
    disabled.directory.clear();
    LoadReport disabledReport;
    const auto uncached = AssetManager::loadProcessed(source, disabled, &disabledReport);
    CHECK(uncached.has_value());
    CHECK(disabledReport.outcome == CacheOutcome::Disabled);
    CHECK(sameProcessed(*cold, *uncached));
}

void testUnwritableDirectoryFallsBack() {
    Sandbox sandbox;
    const std::string source = sandbox.root + "/c.obj";
    assetfixtures::writeBumpySphereObj(source, kRings, kSegments, 0.0f);
    const std::string blocker = sandbox.root + "/blocker";
    std::ofstream(blocker) << "file";
    CacheOptions blocked;
    blocked.directory = blocker + "/cache";
    LoadReport report;
    const auto result = AssetManager::loadProcessed(source, blocked, &report);
    CHECK(result.has_value());
    CHECK(report.outcome == CacheOutcome::Miss);
    CHECK(sameProcessed(*result, AssetManager::processMesh(*AssetManager::importMesh(source))));
}

void testInvalidationOnContentAndSettings() {
    Sandbox sandbox;
    const std::string source = sandbox.root + "/b.obj";
    assetfixtures::writeBumpySphereObj(source, kRings, kSegments, 0.0f);
    LoadReport report;
    const auto original = AssetManager::loadProcessed(source, sandbox.options, &report);
    const std::string originalEntry = report.cachePath;

    assetfixtures::writeBumpySphereObj(source, kRings, kSegments, 1.5f);
    const auto changed = AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(changed.has_value());
    CHECK(report.outcome == CacheOutcome::Miss);
    CHECK(report.cachePath != originalEntry);
    CHECK(!sameProcessed(*original, *changed));

    CacheOptions newerSettings = sandbox.options;
    newerSettings.settingsVersion = sandbox.options.settingsVersion + 1;
    AssetManager::loadProcessed(source, newerSettings, &report);
    CHECK(report.outcome == CacheOutcome::Miss);
    AssetManager::loadProcessed(source, newerSettings, &report);
    CHECK(report.outcome == CacheOutcome::Hit);

    ProcessedMesh scratch{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}};
    const uint64_t contentHash = *AssetCache::hashFile(source);
    const std::string entry = AssetCache::entryPath(sandbox.options.directory, source, contentHash, sandbox.options.settingsVersion);
    CHECK(AssetCache::fetch(entry, contentHash, sandbox.options.settingsVersion + 1, scratch) == CacheOutcome::Invalidated);
    CHECK(AssetCache::fetch(entry, contentHash ^ 1u, sandbox.options.settingsVersion, scratch) == CacheOutcome::Invalidated);
}

void testCorruptionFallsBackToReimport() {
    Sandbox sandbox;
    const std::string source = sandbox.root + "/c.obj";
    assetfixtures::writeBumpySphereObj(source, kRings, kSegments, 0.0f);
    LoadReport report;
    const auto reference = AssetManager::loadProcessed(source, sandbox.options, &report);
    const std::string entry = report.cachePath;
    const uintmax_t fullSize = fs::file_size(entry);

    flipByte(entry, kCorruptOffset);
    auto recovered = AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Corrupt);
    CHECK(recovered.has_value() && sameProcessed(*reference, *recovered));
    AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Hit);

    fs::resize_file(entry, fullSize / 2);
    recovered = AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Corrupt);
    CHECK(recovered.has_value() && sameProcessed(*reference, *recovered));

    { std::ofstream garbage(entry, std::ios::binary | std::ios::trunc); garbage << "not a cache"; }
    recovered = AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Corrupt);
    CHECK(recovered.has_value() && sameProcessed(*reference, *recovered));

    { std::ofstream empty(entry, std::ios::binary | std::ios::trunc); }
    recovered = AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Corrupt);
    CHECK(recovered.has_value());
    AssetManager::loadProcessed(source, sandbox.options, &report);
    CHECK(report.outcome == CacheOutcome::Hit);
}

void testMissingAndBrokenSources() {
    Sandbox sandbox;
    CHECK(!AssetManager::loadProcessed(sandbox.root + "/missing.obj", sandbox.options).has_value());
    const std::string empty = sandbox.root + "/empty.obj";
    { std::ofstream file(empty); file << "# nothing\n"; }
    LoadReport report;
    CHECK(!AssetManager::loadProcessed(empty, sandbox.options, &report).has_value());
    CHECK(report.outcome == CacheOutcome::Failed);
}

void testParallelOrderingAndAtomicity() {
    Sandbox sandbox;
    const std::vector<std::string> sources = assetfixtures::writeObjSet(sandbox.root, 6, kRings, kSegments);
    std::vector<std::string> requested = sources;
    requested.push_back(sandbox.root + "/missing.obj");
    requested.push_back(sources[2]);

    JobSystem serialJobs(0);
    CacheOptions noCache;
    noCache.directory.clear();
    const auto serial = AssetManager::loadProcessedMany(requested, noCache, serialJobs);

    JobSystem parallelJobs(4);
    std::vector<LoadReport> reports;
    const auto parallel = AssetManager::loadProcessedMany(requested, sandbox.options, parallelJobs, &reports);
    CHECK(parallel.size() == requested.size());
    CHECK(!parallel[6].has_value());
    for (size_t slot = 0; slot < requested.size(); ++slot) {
        CHECK(serial[slot].has_value() == parallel[slot].has_value());
        if (serial[slot] && parallel[slot]) CHECK(sameProcessed(*serial[slot], *parallel[slot]));
    }
    CHECK(sameProcessed(*parallel[2], *parallel[7]));

    const auto warm = AssetManager::loadProcessedMany(requested, sandbox.options, parallelJobs, &reports);
    for (size_t slot = 0; slot < sources.size(); ++slot) {
        CHECK(reports[slot].outcome == CacheOutcome::Hit);
        CHECK(warm[slot].has_value() && sameProcessed(*serial[slot], *warm[slot]));
    }

    size_t leftoverTemporaries = 0;
    for (const auto& entry : fs::directory_iterator(sandbox.options.directory)) {
        if (entry.path().extension() == ".tmp") ++leftoverTemporaries;
    }
    CHECK(leftoverTemporaries == 0);
}

}

int main() {
    testHitMissAndIdenticalResults();
    testUnwritableDirectoryFallsBack();
    testInvalidationOnContentAndSettings();
    testCorruptionFallsBackToReimport();
    testMissingAndBrokenSources();
    testParallelOrderingAndAtomicity();
    return testsupport::finish();
}
