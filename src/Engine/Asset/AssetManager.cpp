module;

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

// On-disk format ---------------------------------------------------------
//
// [AssetHeader][vertexFloatCount * float][indexCount * uint32]
//
// Vertices are interleaved: position xyz + normal xyz (6 floats per vertex).
// `vertexFloatCount` is the number of *floats*, not the number of vertices,
// which matches what Mesh::vertices stores.
//
// v1 had no magic/version at all: two bare uint64 counts. Such files are
// rejected by load() (magic mismatch) instead of being misparsed, and bake()
// treats them as out of date so they get regenerated.

constexpr uint32_t kAssetMagic = 0x4D54494CU; // 'L','I','T','M' little-endian
constexpr uint32_t kAssetVersion = 2U;
constexpr uint32_t kFloatsPerVertex = 6U; // position xyz + normal xyz

// Absolute sanity bounds. A corrupt count must never reach an allocator.
// The exact bound is the file size check below; these only keep the
// byte-size arithmetic from overflowing.
constexpr uint64_t kMaxElementCount = 1ULL << 31; // per array

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

// Validates magic/version/layout and that the declared counts agree with the
// real file size. Returns the header on success.
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

    if (header.magic != kAssetMagic) {
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

    if (header.indexCount % 3 != 0) {
        Lit::Log::Error("Asset index count {} is not a multiple of 3: {}", header.indexCount, path);
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

// True when `destinationPath` already holds a valid, current bake of
// `sourcePath`. Skips the (dominant) Assimp import on startup.
bool isBakeUpToDate(const std::string& sourcePath, const std::string& destinationPath) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const auto destStatus = fs::status(destinationPath, ec);
    if (ec || !fs::is_regular_file(destStatus)) return false;

    const auto sourceTime = fs::last_write_time(sourcePath, ec);
    if (ec) return false;
    const auto destTime = fs::last_write_time(destinationPath, ec);
    if (ec) return false;
    if (destTime < sourceTime) return false;

    const auto fileSize = fs::file_size(destinationPath, ec);
    if (ec) return false;

    std::ifstream probe(destinationPath, std::ios::binary);
    if (!probe.is_open()) return false;

    // A stale/foreign/truncated .asset fails validation here and is re-baked.
    return readValidatedHeader(probe, static_cast<uint64_t>(fileSize), destinationPath).has_value();
}

void processMesh(const aiMesh* mesh, std::vector<float>& vertices,
                 std::vector<unsigned int>& indices, size_t& skippedFaces) {
    const size_t baseVertex = vertices.size() / kFloatsPerVertex;
    if (baseVertex + mesh->mNumVertices > std::numeric_limits<unsigned int>::max()) {
        // Index type is uint32; refuse rather than silently wrap.
        skippedFaces += mesh->mNumFaces;
        return;
    }
    const unsigned int baseIndex = static_cast<unsigned int>(baseVertex);

    const bool hasNormals = mesh->HasNormals();
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        vertices.push_back(mesh->mVertices[i].x);
        vertices.push_back(mesh->mVertices[i].y);
        vertices.push_back(mesh->mVertices[i].z);
        if (hasNormals) {
            vertices.push_back(mesh->mNormals[i].x);
            vertices.push_back(mesh->mNormals[i].y);
            vertices.push_back(mesh->mNormals[i].z);
        } else {
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            vertices.push_back(0.0f);
        }
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i]; // by reference: aiFace's copy ctor heap-allocates
        if (face.mNumIndices != 3) {
            // Points/lines survive aiProcess_Triangulate and would corrupt a
            // triangle-list index buffer.
            skippedFaces++;
            continue;
        }
        indices.push_back(baseIndex + face.mIndices[0]);
        indices.push_back(baseIndex + face.mIndices[1]);
        indices.push_back(baseIndex + face.mIndices[2]);
    }
}

void processNode(const aiNode* node, const aiScene* scene, std::vector<float>& vertices,
                 std::vector<unsigned int>& indices, size_t& skippedFaces) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        processMesh(scene->mMeshes[node->mMeshes[i]], vertices, indices, skippedFaces);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, vertices, indices, skippedFaces);
    }
}

} // namespace

bool AssetManager::bake(const std::string& sourcePath, const std::string& destinationPath) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(sourcePath, ec) || ec) {
        Lit::Log::Error("Source model does not exist: {}", sourcePath);
        return false;
    }

    if (isBakeUpToDate(sourcePath, destinationPath)) {
        Lit::Log::Info("Asset is up to date, skipping bake: {}", destinationPath);
        return true;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        sourcePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Lit::Log::Error("ASSIMP failed to load model: {} with error: {}", sourcePath,
                        importer.GetErrorString());
        return false;
    }

    // Reserve up front: the scene knows its totals, so the push_back loops
    // never reallocate.
    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        totalVertices += scene->mMeshes[i]->mNumVertices;
        totalIndices += static_cast<size_t>(scene->mMeshes[i]->mNumFaces) * 3;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(totalVertices * kFloatsPerVertex);
    indices.reserve(totalIndices);

    size_t skippedFaces = 0;
    processNode(scene->mRootNode, scene, vertices, indices, skippedFaces);

    if (skippedFaces != 0) {
        Lit::Log::Warn("Skipped {} non-triangle face(s) while baking {}", skippedFaces, sourcePath);
    }

    if (vertices.empty() || indices.empty()) {
        Lit::Log::Error("Model contains no triangles, refusing to bake: {}", sourcePath);
        return false;
    }

    AssetHeader header{};
    header.magic = kAssetMagic;
    header.version = kAssetVersion;
    header.floatsPerVertex = kFloatsPerVertex;
    header.reserved = 0;
    header.vertexFloatCount = vertices.size();
    header.indexCount = indices.size();

    // Write to a temp file and rename, so a crash mid-write can never leave a
    // half-written .asset that later parses as garbage.
    const fs::path finalPath(destinationPath);
    fs::path tempPath = finalPath;
    tempPath += ".tmp";

    {
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

        if (!outFile.good()) {
            Lit::Log::Error("Failed to write asset data: {}", tempPath.string());
            outFile.close();
            fs::remove(tempPath, ec);
            return false;
        }
    }

    fs::rename(tempPath, finalPath, ec);
    if (ec) {
        Lit::Log::Error("Failed to publish baked asset {}: {}", destinationPath, ec.message());
        fs::remove(tempPath, ec);
        return false;
    }

    Lit::Log::Info("Baked asset: {} ({} vertices, {} indices)", destinationPath,
                   vertices.size() / kFloatsPerVertex, indices.size());
    return true;
}

std::optional<Mesh> AssetManager::load(const std::string& assetPath) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const auto fileSize = fs::file_size(assetPath, ec);
    if (ec) {
        Lit::Log::Warn("Failed to stat asset file {}: {}", assetPath, ec.message());
        return std::nullopt;
    }

    std::ifstream inFile(assetPath, std::ios::binary);
    if (!inFile.is_open()) {
        Lit::Log::Warn("Failed to open file for reading: {}", assetPath);
        return std::nullopt;
    }

    const auto header = readValidatedHeader(inFile, static_cast<uint64_t>(fileSize), assetPath);
    if (!header) return std::nullopt;

    // Both arrays are read straight into their final storage: one bulk read
    // each, no intermediate buffer and no second copy of the data. The header
    // check above guarantees the sizes match the file exactly.
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
    for (unsigned int index : indices) {
        if (index >= vertexCount) {
            Lit::Log::Error("Asset has out-of-range index {} (vertex count {}): {}", index,
                            vertexCount, assetPath);
            return std::nullopt;
        }
    }

    return Mesh(std::move(vertices), std::move(indices));
}
