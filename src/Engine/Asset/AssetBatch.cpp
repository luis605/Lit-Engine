module;

#include "Engine/Log/Log.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

module Engine.asset;
import Engine.mesh;
import Engine.Jobs;

ProcessedMesh AssetManager::processMesh(const Mesh& imported) {
    ProcessedMesh processed{imported.optimized(), {}};
    processed.lods = processed.base.generateLODChain();
    return processed;
}

std::optional<ProcessedMesh> AssetManager::loadProcessed(const std::string& sourcePath, const CacheOptions& options, LoadReport* report) {
    LoadReport local;
    LoadReport& outcomeReport = report ? *report : local;
    outcomeReport = LoadReport{};

    const std::optional<uint64_t> contentHash = AssetCache::hashFile(sourcePath);
    if (!contentHash) {
        Lit::Log::Error("Source model unreadable: {}", sourcePath);
        return std::nullopt;
    }

    const bool cacheEnabled = !options.directory.empty();
    outcomeReport.outcome = CacheOutcome::Disabled;
    if (cacheEnabled) {
        outcomeReport.cachePath = AssetCache::entryPath(options.directory, sourcePath, *contentHash, options.settingsVersion);
        ProcessedMesh cached{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}};
        outcomeReport.outcome = AssetCache::fetch(outcomeReport.cachePath, *contentHash, options.settingsVersion, cached);
        if (outcomeReport.outcome == CacheOutcome::Hit) return cached;
        if (outcomeReport.outcome == CacheOutcome::Corrupt) Lit::Log::Warn("Corrupt mesh cache entry, reimporting: {}", outcomeReport.cachePath);
    }

    const std::optional<Mesh> imported = importMesh(sourcePath);
    if (!imported) {
        outcomeReport.outcome = CacheOutcome::Failed;
        return std::nullopt;
    }
    ProcessedMesh processed = processMesh(*imported);
    if (cacheEnabled && !AssetCache::store(outcomeReport.cachePath, *contentHash, options.settingsVersion, processed)) {
        Lit::Log::Warn("Failed to write mesh cache entry: {}", outcomeReport.cachePath);
    }
    return processed;
}

std::vector<std::optional<ProcessedMesh>> AssetManager::loadProcessedMany(const std::vector<std::string>& sourcePaths, const CacheOptions& options, JobSystem& jobs, std::vector<LoadReport>* reports) {
    std::vector<std::optional<ProcessedMesh>> results(sourcePaths.size());
    std::vector<LoadReport> localReports(sourcePaths.size());
    constexpr size_t kOneAssetPerTask = 1;
    jobs.parallelFor(sourcePaths.size(), kOneAssetPerTask, [&](size_t begin, size_t end) {
        for (size_t slot = begin; slot < end; ++slot) results[slot] = loadProcessed(sourcePaths[slot], options, &localReports[slot]);
    });
    if (reports) *reports = std::move(localReports);
    return results;
}
