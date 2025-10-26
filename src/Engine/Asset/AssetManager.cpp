module;

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <vector>
#include <iostream>
#include <optional>
#include <fstream>

module Engine.asset;
import Engine.mesh;

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
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }
}

void processNode(aiNode* node, const aiScene* scene, std::vector<float>& vertices,
                 std::vector<unsigned int>& indices) {
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
    const aiScene* scene = importer.ReadFile(sourcePath, aiProcess_Triangulate | aiProcess_FlipUVs |
                                                             aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return false;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    processNode(scene->mRootNode, scene, vertices, indices);

    std::ofstream outFile(destinationPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "ERROR::ASSET_BAKER: Failed to open file for writing: " << destinationPath
                  << std::endl;
        return false;
    }

    AssetHeader header;
    header.vertexCount = vertices.size();
    header.indexCount = indices.size();

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(AssetHeader));
    outFile.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(float));
    outFile.write(reinterpret_cast<const char*>(indices.data()),
                  indices.size() * sizeof(unsigned int));

    outFile.close();

    std::cout << "Successfully baked asset: " << destinationPath << std::endl;
    return true;
}

std::optional<Mesh> AssetManager::load(const std::string& assetPath) {
    std::ifstream inFile(assetPath, std::ios::binary);
    if (!inFile.is_open()) {
        std::cerr << "ERROR::ASSET_LOADER: Failed to open file for reading: " << assetPath
                  << std::endl;
        return std::nullopt;
    }

    AssetHeader header;
    inFile.read(reinterpret_cast<char*>(&header), sizeof(AssetHeader));

    if (header.vertexCount == 0 || header.indexCount == 0) {
        std::cerr << "ERROR::ASSET_LOADER: Asset file is empty or corrupted: " << assetPath
                  << std::endl;
        return std::nullopt;
    }

    std::vector<float> vertices(header.vertexCount);
    inFile.read(reinterpret_cast<char*>(vertices.data()), vertices.size() * sizeof(float));

    std::vector<unsigned int> indices(header.indexCount);
    inFile.read(reinterpret_cast<char*>(indices.data()), indices.size() * sizeof(unsigned int));

    inFile.close();

    return Mesh(std::move(vertices), std::move(indices));
}
