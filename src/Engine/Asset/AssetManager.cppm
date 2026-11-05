module;
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

export module Engine.asset;

import Engine.mesh;
import Engine.Jobs;

export inline constexpr uint32_t kImportSettingsVersion = 1;
export inline constexpr const char* kDefaultCacheDirectory = ".litcache";

export struct ProcessedMesh {
    Mesh base;
    std::vector<Mesh> lods;
};

export struct CacheOptions {
    std::string directory = kDefaultCacheDirectory;
    uint32_t settingsVersion = kImportSettingsVersion;
};

export enum class CacheOutcome : uint8_t { Hit, Miss, Corrupt, Invalidated, Disabled, Failed };

export struct LoadReport {
    CacheOutcome outcome = CacheOutcome::Failed;
    std::string cachePath;
};

export namespace AssetManager {
bool bake(const std::string& sourcePath, const std::string& destinationPath);
std::optional<Mesh> load(const std::string& assetPath);

std::optional<Mesh> importMesh(const std::string& sourcePath);
ProcessedMesh processMesh(const Mesh& imported);
std::optional<ProcessedMesh> loadProcessed(const std::string& sourcePath, const CacheOptions& options, LoadReport* report = nullptr);
std::vector<std::optional<ProcessedMesh>> loadProcessedMany(const std::vector<std::string>& sourcePaths, const CacheOptions& options, JobSystem& jobs, std::vector<LoadReport>* reports = nullptr);
}

export namespace AssetCache {
uint64_t hashBytes(const void* data, size_t byteCount);
std::optional<uint64_t> hashFile(const std::string& path);
std::string entryPath(const std::string& directory, const std::string& sourcePath, uint64_t contentHash, uint32_t settingsVersion);
bool store(const std::string& path, uint64_t contentHash, uint32_t settingsVersion, const ProcessedMesh& processed);
CacheOutcome fetch(const std::string& path, uint64_t contentHash, uint32_t settingsVersion, ProcessedMesh& out);
}
