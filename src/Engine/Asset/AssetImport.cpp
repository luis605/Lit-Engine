module;

#include <cmath>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Engine/Log/Log.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

module Engine.asset;
import Engine.mesh;

namespace {

constexpr uint32_t kFloatsPerVertex = 8;
constexpr uint32_t kIndicesPerTriangle = 3;
constexpr float kDefaultNormalY = 1.0f;
constexpr unsigned int kImportFlags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals;

aiVector3D planarUv(const aiVector3D& position, const aiVector3D& normal) {
    const float absX = std::abs(normal.x);
    const float absY = std::abs(normal.y);
    const float absZ = std::abs(normal.z);
    if (absX >= absY && absX >= absZ) return aiVector3D(position.z, position.y, 0.0f);
    if (absY >= absZ) return aiVector3D(position.x, position.z, 0.0f);
    return aiVector3D(position.x, position.y, 0.0f);
}

void appendVertices(const aiMesh* mesh, std::vector<float>& vertices) {
    const bool hasNormals = mesh->HasNormals();
    const bool hasUvs = mesh->HasTextureCoords(0);
    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; vertexIndex++) {
        const aiVector3D& position = mesh->mVertices[vertexIndex];
        const aiVector3D normal = hasNormals ? mesh->mNormals[vertexIndex] : aiVector3D(0.0f, kDefaultNormalY, 0.0f);
        const aiVector3D uv = hasUvs ? mesh->mTextureCoords[0][vertexIndex] : planarUv(position, normal);
        vertices.insert(vertices.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z, uv.x, uv.y});
    }
}

void appendMesh(const aiMesh* mesh, std::vector<float>& vertices, std::vector<unsigned int>& indices, size_t& skippedFaces) {
    const size_t baseVertex = vertices.size() / kFloatsPerVertex;
    if (baseVertex + mesh->mNumVertices > std::numeric_limits<unsigned int>::max()) {
        skippedFaces += mesh->mNumFaces;
        return;
    }
    const unsigned int baseIndex = static_cast<unsigned int>(baseVertex);

    appendVertices(mesh, vertices);

    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; faceIndex++) {
        const aiFace& face = mesh->mFaces[faceIndex];
        if (face.mNumIndices != kIndicesPerTriangle) {
            skippedFaces++;
            continue;
        }
        indices.push_back(baseIndex + face.mIndices[0]);
        indices.push_back(baseIndex + face.mIndices[1]);
        indices.push_back(baseIndex + face.mIndices[2]);
    }
}

void appendNode(const aiNode* node, const aiScene* scene, std::vector<float>& vertices,
                std::vector<unsigned int>& indices, size_t& skippedFaces) {
    for (unsigned int meshSlot = 0; meshSlot < node->mNumMeshes; meshSlot++) {
        appendMesh(scene->mMeshes[node->mMeshes[meshSlot]], vertices, indices, skippedFaces);
    }
    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; childIndex++) {
        appendNode(node->mChildren[childIndex], scene, vertices, indices, skippedFaces);
    }
}

}

std::optional<Mesh> AssetManager::importMesh(const std::string& sourcePath) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath, kImportFlags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        Lit::Log::Error("ASSIMP failed to load model: {} with error: {}", sourcePath, importer.GetErrorString());
        return std::nullopt;
    }

    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++) {
        totalVertices += scene->mMeshes[meshIndex]->mNumVertices;
        totalIndices += static_cast<size_t>(scene->mMeshes[meshIndex]->mNumFaces) * kIndicesPerTriangle;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(totalVertices * kFloatsPerVertex);
    indices.reserve(totalIndices);

    size_t skippedFaces = 0;
    appendNode(scene->mRootNode, scene, vertices, indices, skippedFaces);

    if (skippedFaces != 0) Lit::Log::Warn("Skipped {} non-triangle face(s) while importing {}", skippedFaces, sourcePath);
    if (vertices.empty() || indices.empty()) {
        Lit::Log::Error("Model contains no triangles: {}", sourcePath);
        return std::nullopt;
    }
    return Mesh(std::move(vertices), std::move(indices));
}
