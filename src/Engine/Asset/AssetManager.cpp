module;

#include "Engine/Log/Log.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

module Engine.asset;
import Engine.mesh;

namespace {

constexpr uint32_t makeSignature(const char text[5]) {
    return static_cast<uint32_t>(text[0]) |
            (static_cast<uint32_t>(text[1]) << 8) |
            (static_cast<uint32_t>(text[2]) << 16) |
            (static_cast<uint32_t>(text[3]) << 24);
}

constexpr uint32_t kAssetFileSignature = makeSignature("LITM");
constexpr uint32_t kAssetVersion = 3;
constexpr uint32_t kFloatsPerVertex = 8;

constexpr uint64_t kMaxElementCount = 1ULL << 31;
constexpr uint32_t kIndicesPerTriangle = 3;

struct AssetHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t floatsPerVertex;
    uint32_t reserved;
    uint64_t vertexFloatCount;
    uint64_t indexCount;
};
static_assert(sizeof(AssetHeader) == 32, "AssetHeader must stay 32 bytes on disk");

bool readExactly(std::istream& stream, void* destination, std::streamsize bytes) {
    if (bytes == 0) return true;
    stream.read(static_cast<char*>(destination), bytes);
    return stream.good() && stream.gcount() == bytes;
}

std::optional<AssetHeader> readValidatedHeader(std::istream& stream,
                                               uint64_t fileSize,
                                               const std::string& path) {
    if (fileSize < sizeof(AssetHeader)) {
        Lit::Log::Warn("Asset file is truncated (only {} bytes): {}", fileSize, path);
        return std::nullopt;
    }

    AssetHeader header{};
    if (!readExactly(stream, &header, sizeof(AssetHeader))) {
        Lit::Log::Warn("Failed to read asset header: {}", path);
        return std::nullopt;
    }

    if (header.magic != kAssetFileSignature) {
        Lit::Log::Error("Not a Lit asset file (bad magic 0x{:08X}): {}", header.magic, path);
        return std::nullopt;
    }

    if (header.version != kAssetVersion) {
        Lit::Log::Error("Unsupported asset version {} (expected {}), please re-bake: {}",
                        header.version, kAssetVersion, path);
        return std::nullopt;
    }

    if (header.floatsPerVertex != kFloatsPerVertex) {
        Lit::Log::Error("Unsupported vertex layout ({} floats per vertex, expected {}): {}",
                        header.floatsPerVertex, kFloatsPerVertex, path);
        return std::nullopt;
    }

    if (header.vertexFloatCount == 0 || header.indexCount == 0) {
        Lit::Log::Warn("Asset file contains no geometry: {}", path);
        return std::nullopt;
    }

    if (header.vertexFloatCount > kMaxElementCount || header.indexCount > kMaxElementCount) {
        Lit::Log::Error("Asset element counts are implausible (v={}, i={}): {}",
                        header.vertexFloatCount, header.indexCount, path);
        return std::nullopt;
    }

    if (header.vertexFloatCount % kFloatsPerVertex != 0) {
        Lit::Log::Error("Asset vertex float count {} is not a multiple of {}: {}",
                        header.vertexFloatCount, kFloatsPerVertex, path);
        return std::nullopt;
    }

    if (header.indexCount % kIndicesPerTriangle != 0) {
        Lit::Log::Error("Asset index count {} is not a multiple of {}: {}", header.indexCount, kIndicesPerTriangle, path);
        return std::nullopt;
    }

    const uint64_t expectedSize = sizeof(AssetHeader) +
                                  header.vertexFloatCount * sizeof(float) +
                                  header.indexCount * sizeof(uint32_t);
    if (expectedSize != fileSize) {
        Lit::Log::Error("Asset file size mismatch ({} bytes on disk, {} expected): {}",
                        fileSize, expectedSize, path);
        return std::nullopt;
    }

    return header;
}

bool isBakeUpToDate(const std::string& sourcePath, const std::string& destinationPath) {
    namespace fs = std::filesystem;
    std::error_code error;

    const auto destinationStatus = fs::status(destinationPath, error);
    if (error || !fs::is_regular_file(destinationStatus)) return false;

    const auto sourceTime = fs::last_write_time(sourcePath, error);
    if (error) return false;
    const auto destinationTime = fs::last_write_time(destinationPath, error);
    if (error) return false;
    if (destinationTime < sourceTime) return false;

    const auto fileSize = fs::file_size(destinationPath, error);
    if (error) return false;

    std::ifstream probe(destinationPath, std::ios::binary);
    if (!probe.is_open()) return false;
    AssetHeader peek{};
    if (!readExactly(probe, &peek, sizeof(peek)) || peek.magic != kAssetFileSignature || peek.version != kAssetVersion || peek.floatsPerVertex != kFloatsPerVertex) return false;
    probe.seekg(0);

    return readValidatedHeader(probe, static_cast<uint64_t>(fileSize), destinationPath).has_value();
}

bool writeAssetFile(const std::filesystem::path& tempPath, const AssetHeader& header,
                    const std::vector<float>& vertices, const std::vector<unsigned int>& indices) {
    std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
    if (!outFile.is_open()) {
        Lit::Log::Error("Failed to open file for writing: {}", tempPath.string());
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(AssetHeader));
    outFile.write(reinterpret_cast<const char*>(vertices.data()),
                  static_cast<std::streamsize>(vertices.size() * sizeof(float)));
    outFile.write(reinterpret_cast<const char*>(indices.data()),
                  static_cast<std::streamsize>(indices.size() * sizeof(unsigned int)));
    outFile.flush();

    if (outFile.good()) return true;
    Lit::Log::Error("Failed to write asset data: {}", tempPath.string());
    outFile.close();
    std::error_code removeError;
    std::filesystem::remove(tempPath, removeError);
    return false;
}

}

bool AssetManager::bake(const std::string& sourcePath, const std::string& destinationPath) {
    namespace fs = std::filesystem;
    std::error_code error;

    if (!fs::exists(sourcePath, error) || error) {
        Lit::Log::Error("Source model does not exist: {}", sourcePath);
        return false;
    }

    if (isBakeUpToDate(sourcePath, destinationPath)) {
        Lit::Log::Info("Asset is up to date, skipping bake: {}", destinationPath);
        return true;
    }

    const std::optional<Mesh> imported = importMesh(sourcePath);
    if (!imported) return false;
    const std::vector<float>& vertices = imported->vertices;
    const std::vector<unsigned int>& indices = imported->indices;

    AssetHeader header{};
    header.magic = kAssetFileSignature;
    header.version = kAssetVersion;
    header.floatsPerVertex = kFloatsPerVertex;
    header.reserved = 0;
    header.vertexFloatCount = vertices.size();
    header.indexCount = indices.size();

    const fs::path finalPath(destinationPath);
    fs::path tempPath = finalPath;
    tempPath += ".tmp";

    if (!writeAssetFile(tempPath, header, vertices, indices)) return false;

    fs::rename(tempPath, finalPath, error);
    if (error) {
        Lit::Log::Error("Failed to publish baked asset {}: {}", destinationPath, error.message());
        fs::remove(tempPath, error);
        return false;
    }

    Lit::Log::Info("Baked asset: {} ({} vertices, {} indices)", destinationPath,
                   vertices.size() / kFloatsPerVertex, indices.size());
    return true;
}

std::optional<Mesh> AssetManager::load(const std::string& assetPath) {
    namespace fs = std::filesystem;
    std::error_code error;

    const auto fileSize = fs::file_size(assetPath, error);
    if (error) {
        Lit::Log::Warn("Failed to stat asset file {}: {}", assetPath, error.message());
        return std::nullopt;
    }

    std::ifstream inFile(assetPath, std::ios::binary);
    if (!inFile.is_open()) {
        Lit::Log::Warn("Failed to open file for reading: {}", assetPath);
        return std::nullopt;
    }

    const auto header = readValidatedHeader(inFile, static_cast<uint64_t>(fileSize), assetPath);
    if (!header) return std::nullopt;

    std::vector<float> vertices(header->vertexFloatCount);
    if (!readExactly(inFile, vertices.data(),
                     static_cast<std::streamsize>(vertices.size() * sizeof(float)))) {
        Lit::Log::Error("Truncated vertex data in asset: {}", assetPath);
        return std::nullopt;
    }

    std::vector<unsigned int> indices(header->indexCount);
    if (!readExactly(inFile, indices.data(),
                     static_cast<std::streamsize>(indices.size() * sizeof(unsigned int)))) {
        Lit::Log::Error("Truncated index data in asset: {}", assetPath);
        return std::nullopt;
    }

    const size_t vertexCount = vertices.size() / kFloatsPerVertex;
    for (const unsigned int index : indices) {
        if (index >= vertexCount) {
            Lit::Log::Error("Asset has out-of-range index {} (vertex count {}): {}", index,
                            vertexCount, assetPath);
            return std::nullopt;
        }
    }

    return Mesh(std::move(vertices), std::move(indices));
}
