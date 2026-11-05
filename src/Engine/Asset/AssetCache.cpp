module;

#include "Engine/Log/Log.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

module Engine.asset;
import Engine.mesh;

namespace {

constexpr uint32_t kCacheMagic = 0x434D494C;
constexpr uint32_t kCacheFormatVersion = 1;
constexpr uint64_t kHashSeed = 0x9E3779B97F4A7C15ULL;
constexpr uint64_t kHashMultiplier = 0xFF51AFD7ED558CCDULL;
constexpr uint64_t kHashFinalMultiplier = 0xC4CEB9FE1A85EC53ULL;
constexpr unsigned kHashShift = 32;
constexpr uint64_t kMaxElementCount = 1ULL << 31;
constexpr uint64_t kMaxLevelCount = 16;
constexpr size_t kHexDigits = 16;
constexpr std::streamsize kMaxCacheFileBytes = std::streamsize(1) << 32;

struct CacheHeader {
    uint32_t magic;
    uint32_t formatVersion;
    uint32_t settingsVersion;
    uint32_t levelCount;
    uint64_t contentHash;
    uint64_t payloadBytes;
    uint64_t payloadChecksum;
    uint64_t reserved;
};
static_assert(sizeof(CacheHeader) == 48, "CacheHeader must stay 48 bytes on disk");

struct LevelHeader {
    uint64_t vertexFloatCount;
    uint64_t indexCount;
};

uint64_t mixWord(uint64_t state, uint64_t word) {
    state ^= word;
    state *= kHashMultiplier;
    return state ^ (state >> kHashShift);
}

std::string hexString(uint64_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text(kHexDigits, '0');
    for (size_t digit = 0; digit < kHexDigits; ++digit) text[kHexDigits - 1 - digit] = kDigits[(value >> (digit * 4)) & 0xF];
    return text;
}

void appendBytes(std::vector<char>& buffer, const void* data, size_t byteCount) {
    const char* bytes = static_cast<const char*>(data);
    buffer.insert(buffer.end(), bytes, bytes + byteCount);
}

void appendLevel(std::vector<char>& buffer, const Mesh& mesh) {
    const LevelHeader level{mesh.vertices.size(), mesh.indices.size()};
    appendBytes(buffer, &level, sizeof(level));
    appendBytes(buffer, mesh.vertices.data(), mesh.vertices.size() * sizeof(float));
    appendBytes(buffer, mesh.indices.data(), mesh.indices.size() * sizeof(unsigned int));
}

std::string temporaryPathFor(const std::string& finalPath) {
    static std::atomic<uint64_t> counter{0};
    const uint64_t threadKey = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return finalPath + "." + hexString(threadKey ^ counter.fetch_add(1)) + ".tmp";
}

bool readLevel(const char*& cursor, const char* end, Mesh& out) {
    LevelHeader level{};
    if (static_cast<size_t>(end - cursor) < sizeof(level)) return false;
    std::memcpy(&level, cursor, sizeof(level));
    cursor += sizeof(level);

    if (level.vertexFloatCount > kMaxElementCount || level.indexCount > kMaxElementCount) return false;
    if (level.vertexFloatCount % kVertexFloats != 0 || level.indexCount % 3 != 0) return false;
    const uint64_t levelBytes = level.vertexFloatCount * sizeof(float) + level.indexCount * sizeof(unsigned int);
    if (levelBytes > static_cast<uint64_t>(end - cursor)) return false;

    std::vector<float> vertices(level.vertexFloatCount);
    std::vector<unsigned int> indices(level.indexCount);
    std::memcpy(vertices.data(), cursor, vertices.size() * sizeof(float));
    cursor += vertices.size() * sizeof(float);
    std::memcpy(indices.data(), cursor, indices.size() * sizeof(unsigned int));
    cursor += indices.size() * sizeof(unsigned int);

    const uint64_t vertexCount = vertices.size() / kVertexFloats;
    for (const unsigned int index : indices) {
        if (index >= vertexCount) return false;
    }
    out = Mesh(std::move(vertices), std::move(indices));
    return true;
}

}

uint64_t AssetCache::hashBytes(const void* data, size_t byteCount) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    uint64_t state = kHashSeed ^ byteCount;
    size_t offset = 0;
    for (; offset + sizeof(uint64_t) <= byteCount; offset += sizeof(uint64_t)) {
        uint64_t word;
        std::memcpy(&word, bytes + offset, sizeof(word));
        state = mixWord(state, word);
    }
    uint64_t tail = 0;
    std::memcpy(&tail, bytes + offset, byteCount - offset);
    state = mixWord(state, tail);
    state *= kHashFinalMultiplier;
    return state ^ (state >> kHashShift);
}

std::optional<uint64_t> AssetCache::hashFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;
    const std::streamsize size = file.tellg();
    if (size < 0 || size > kMaxCacheFileBytes) return std::nullopt;
    std::vector<char> content(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(content.data(), size)) return std::nullopt;
    return hashBytes(content.data(), content.size());
}

std::string AssetCache::entryPath(const std::string& directory, const std::string& sourcePath, uint64_t contentHash, uint32_t settingsVersion) {
    const std::string stem = std::filesystem::path(sourcePath).stem().string();
    const std::string name = stem + "-" + hexString(contentHash) + "-v" + std::to_string(settingsVersion) + ".litmesh";
    return (std::filesystem::path(directory) / name).string();
}

bool AssetCache::store(const std::string& path, uint64_t contentHash, uint32_t settingsVersion, const ProcessedMesh& processed) {
    namespace fs = std::filesystem;
    std::vector<char> payload;
    appendLevel(payload, processed.base);
    for (const Mesh& lod : processed.lods) appendLevel(payload, lod);

    CacheHeader header{};
    header.magic = kCacheMagic;
    header.formatVersion = kCacheFormatVersion;
    header.settingsVersion = settingsVersion;
    header.levelCount = static_cast<uint32_t>(1 + processed.lods.size());
    header.contentHash = contentHash;
    header.payloadBytes = payload.size();
    header.payloadChecksum = hashBytes(payload.data(), payload.size());

    std::error_code error;
    fs::create_directories(fs::path(path).parent_path(), error);
    const std::string temporaryPath = temporaryPathFor(path);
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        file.flush();
        if (!file.good()) {
            file.close();
            fs::remove(temporaryPath, error);
            return false;
        }
    }
    fs::rename(temporaryPath, path, error);
    if (error) {
        fs::remove(temporaryPath, error);
        return false;
    }
    return true;
}

CacheOutcome AssetCache::fetch(const std::string& path, uint64_t contentHash, uint32_t settingsVersion, ProcessedMesh& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return CacheOutcome::Miss;
    const std::streamsize size = file.tellg();
    if (size < static_cast<std::streamsize>(sizeof(CacheHeader)) || size > kMaxCacheFileBytes) return CacheOutcome::Corrupt;
    std::vector<char> content(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(content.data(), size)) return CacheOutcome::Corrupt;

    CacheHeader header{};
    std::memcpy(&header, content.data(), sizeof(header));
    if (header.magic != kCacheMagic || header.formatVersion != kCacheFormatVersion) return CacheOutcome::Corrupt;
    if (header.settingsVersion != settingsVersion || header.contentHash != contentHash) return CacheOutcome::Invalidated;
    if (header.levelCount == 0 || header.levelCount > kMaxLevelCount) return CacheOutcome::Corrupt;

    const char* cursor = content.data() + sizeof(header);
    const char* end = content.data() + content.size();
    if (header.payloadBytes != static_cast<uint64_t>(end - cursor)) return CacheOutcome::Corrupt;
    if (hashBytes(cursor, header.payloadBytes) != header.payloadChecksum) return CacheOutcome::Corrupt;

    std::vector<Mesh> levels;
    levels.reserve(header.levelCount);
    for (uint32_t level = 0; level < header.levelCount; ++level) {
        Mesh mesh(std::vector<float>{}, std::vector<unsigned int>{});
        if (!readLevel(cursor, end, mesh)) return CacheOutcome::Corrupt;
        levels.push_back(std::move(mesh));
    }
    if (cursor != end || levels.front().indices.empty()) return CacheOutcome::Corrupt;

    out.base = std::move(levels.front());
    out.lods.assign(std::make_move_iterator(levels.begin() + 1), std::make_move_iterator(levels.end()));
    return CacheOutcome::Hit;
}
