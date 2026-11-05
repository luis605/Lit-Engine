#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include "AssetFixtures.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

import Engine.asset;
import Engine.mesh;

namespace {

namespace fs = std::filesystem;

constexpr size_t kHeaderBytes = 48;
constexpr size_t kLevelCountOffset = 12;
constexpr size_t kPayloadBytesOffset = 24;
constexpr uint64_t kOversizedBytes = uint64_t(1) << 40;
constexpr int kWriterThreads = 8;
constexpr int kWritesPerThread = 25;

struct Sandbox {
    std::string root;
    Sandbox() {
        root = testsupport::tempFilePath("litreview2_", "assets");
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Sandbox() {
        std::error_code ignored;
        fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ignored);
        fs::remove_all(root, ignored);
    }
};

std::vector<char> readAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::vector<char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

struct Fixture {
    Sandbox sandbox;
    ProcessedMesh processed;
    std::string cachePath;
    Fixture() : processed{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}} {
        const std::string source = sandbox.root + "/ball.obj";
        assetfixtures::writeBumpySphereObj(source, 6, 8, 0.0f);
        CacheOptions options;
        options.directory = sandbox.root + "/cache";
        auto loaded = AssetManager::loadProcessed(source, options);
        if (loaded) processed = std::move(*loaded);
        for (const auto& entry : fs::directory_iterator(options.directory)) cachePath = entry.path().string();
    }
};

void testOversizedAndLyingFiles() {
    Fixture fixture;
    CHECK(!fixture.cachePath.empty());
    const std::vector<char> good = readAll(fixture.cachePath);
    CHECK(good.size() > kHeaderBytes);
    ProcessedMesh out{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}};
    const std::string probe = fixture.sandbox.root + "/probe.litmesh";

    fs::copy_file(fixture.cachePath, probe, fs::copy_options::overwrite_existing);
    CHECK(AssetCache::fetch(probe, 0, 0, out) == CacheOutcome::Invalidated || AssetCache::fetch(probe, 0, 0, out) == CacheOutcome::Corrupt);

    std::vector<char> huge = good;
    fs::resize_file(probe, kOversizedBytes);
    CHECK(AssetCache::fetch(probe, 1, 1, out) == CacheOutcome::Corrupt);
    CHECK(!AssetCache::hashFile(probe).has_value());

    for (size_t cut = 0; cut < good.size(); cut += (cut < kHeaderBytes + 40 ? 1 : 37)) {
        writeAll(probe, std::vector<char>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut)));
        const CacheOutcome outcome = AssetCache::fetch(probe, 0, 0, out);
        CHECK(outcome != CacheOutcome::Hit);
    }

    uint32_t settings = 0;
    uint64_t contentHash = 0;
    std::memcpy(&settings, good.data() + 8, sizeof(settings));
    std::memcpy(&contentHash, good.data() + 16, sizeof(contentHash));

    std::vector<char> lying = good;
    const uint64_t claimed = ~uint64_t(0);
    std::memcpy(lying.data() + kPayloadBytesOffset, &claimed, sizeof(claimed));
    writeAll(probe, lying);
    CHECK(AssetCache::fetch(probe, contentHash, settings, out) == CacheOutcome::Corrupt);

    lying = good;
    const uint32_t manyLevels = 0xFFFFFFFFu;
    std::memcpy(lying.data() + kLevelCountOffset, &manyLevels, sizeof(manyLevels));
    writeAll(probe, lying);
    CHECK(AssetCache::fetch(probe, contentHash, settings, out) == CacheOutcome::Corrupt);

    std::vector<char> garbage(good.size());
    for (size_t index = 0; index < garbage.size(); ++index) garbage[index] = static_cast<char>((index * 2654435761u) >> 13);
    writeAll(probe, garbage);
    CHECK(AssetCache::fetch(probe, contentHash, settings, out) == CacheOutcome::Corrupt);

    CHECK(AssetCache::fetch(fixture.sandbox.root, contentHash, settings, out) != CacheOutcome::Hit);
    CHECK(AssetCache::fetch(fixture.cachePath, contentHash, settings, out) == CacheOutcome::Hit);
}

void testReadOnlyDirectory() {
    Fixture fixture;
    const std::string locked = fixture.sandbox.root + "/locked";
    fs::create_directories(locked);
    ::chmod(locked.c_str(), 0555);
    const std::string probeFile = locked + "/probe";
    std::ofstream attempt(probeFile);
    const bool canWrite = attempt.is_open();
    attempt.close();
    if (!canWrite) {
        CHECK(!AssetCache::store(locked + "/x.litmesh", 1, 1, fixture.processed));
        CacheOptions options;
        options.directory = locked + "/cache";
        const std::string source = fixture.sandbox.root + "/ball.obj";
        LoadReport report;
        auto loaded = AssetManager::loadProcessed(source, options, &report);
        CHECK(loaded.has_value());
    }
    ::chmod(locked.c_str(), 0755);
}

void testConcurrentWriters() {
    Fixture fixture;
    const std::string shared = fixture.sandbox.root + "/shared.litmesh";
    std::atomic<int> badReads{0};
    std::atomic<int> failedStores{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kWriterThreads; ++thread) {
        threads.emplace_back([&, thread] {
            ProcessedMesh out{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}};
            for (int round = 0; round < kWritesPerThread; ++round) {
                if (!AssetCache::store(shared, 7, 3, fixture.processed)) ++failedStores;
                const CacheOutcome outcome = AssetCache::fetch(shared, 7, 3, out);
                if (outcome != CacheOutcome::Hit && outcome != CacheOutcome::Miss) ++badReads;
            }
            (void)thread;
        });
    }
    for (std::thread& worker : threads) worker.join();
    CHECK(badReads == 0);
    CHECK(failedStores == 0);
    int leftovers = 0;
    for (const auto& entry : fs::directory_iterator(fixture.sandbox.root)) leftovers += entry.path().extension() == ".tmp" ? 1 : 0;
    CHECK(leftovers == 0);
}

void testHashCollisionSafety() {
    Fixture fixture;
    ProcessedMesh out{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}};
    const std::string path = fixture.sandbox.root + "/c.litmesh";
    CHECK(AssetCache::store(path, 5, 1, fixture.processed));
    CHECK(AssetCache::fetch(path, 6, 1, out) == CacheOutcome::Invalidated);
    CHECK(AssetCache::fetch(path, 5, 2, out) == CacheOutcome::Invalidated);
    CHECK(AssetCache::entryPath("", "a/b/model.obj", 1, 1).find("model-") != std::string::npos);
    CHECK(AssetCache::hashBytes(nullptr, 0) == AssetCache::hashBytes("", 0));
}

}

void runReview2AssetTests() {
    testOversizedAndLyingFiles();
    testReadOnlyDirectory();
    testConcurrentWriters();
    testHashCollisionSafety();
}
