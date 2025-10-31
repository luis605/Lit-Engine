module;

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <vector>
#include <optional>
#include <fstream>

module Engine.asset;
import Engine.mesh;
import Engine.Log;

namespace {

struct AssetHeader {
    uint64_t vertexCount;
    uint64_t indexCount;
};

void processMesh(aiMesh* mesh, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        vertices.push_back(mesh->mVertices[i].x);
        vertices.push_back(mesh->mVertices[i].y);
        vertices.push_back(mesh->mVertices[i].z);
        if (mesh->HasNormals()) {
            vertices.push_back(mesh->mNormals[i].x);
            vertices.push_back(mesh->mNormals[i].y);
            vertices.push_back(mesh->mNormals[i].z);
        }
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }
}

void processNode(aiNode* node, const aiScene* scene, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, vertices, indices);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, vertices, indices);
    }
}

} // namespace

bool AssetManager::bake(const std::string& sourcePath, const std::string& destinationPath) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Lit::Log::Error("ASSIMP failed to load model: {} with error: {}", sourcePath, importer.GetErrorString());
        return false;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    processNode(scene->mRootNode, scene, vertices, indices);

    std::ofstream outFile(destinationPath, std::ios::binary);
    if (!outFile.is_open()) {
        Lit::Log::Error("Failed to open file for writing: {}", destinationPath);
        return false;
    }

    AssetHeader header;
    header.vertexCount = vertices.size();
    header.indexCount = indices.size();

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(AssetHeader));
    outFile.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(float));
    outFile.write(reinterpret_cast<const char*>(indices.data()), indices.size() * sizeof(unsigned int));

    outFile.close();

    Lit::Log::Info("Baking asset: {}", destinationPath);
    return true;
}

std::optional<Mesh> AssetManager::load(const std::string& assetPath) {
    std::ifstream inFile(assetPath, std::ios::binary);
    if (!inFile.is_open()) {
        Lit::Log::Warn("Failed to open file for reading: {}", assetPath);
        return std::nullopt;
    }

    AssetHeader header;
    inFile.read(reinterpret_cast<char*>(&header), sizeof(AssetHeader));

    if (header.vertexCount == 0 || header.indexCount == 0) {
        Lit::Log::Warn("Asset file is empty or corrupted: {}", assetPath);
        return std::nullopt;
    }

    std::vector<float> vertices(header.vertexCount);
    inFile.read(reinterpret_cast<char*>(vertices.data()), vertices.size() * sizeof(float));

    std::vector<unsigned int> indices(header.indexCount);
    inFile.read(reinterpret_cast<char*>(indices.data()), indices.size() * sizeof(unsigned int));

    inFile.close();

    return Mesh(std::move(vertices), std::move(indices));
}
