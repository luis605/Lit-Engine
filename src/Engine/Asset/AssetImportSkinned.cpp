module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/anim.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Engine/Log/Log.hpp"

module Engine.asset;

import Engine.glm;
import Engine.mesh;
import Engine.Skin;

namespace {

constexpr unsigned int kSkinnedImportFlags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_LimitBoneWeights;
constexpr float kDefaultTicksPerSecond = 25.0f;

glm::mat4 toGlm(const aiMatrix4x4& m) {
    glm::mat4 out(1.0f);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) out[c][r] = m[r][c];
    }
    return out;
}

struct Skeletons {
    std::vector<SkinJoint> joints;
    std::unordered_map<std::string, int> byName;
};

bool subtreeHasBone(const aiNode* node, const std::unordered_map<std::string, const aiBone*>& bones) {
    if (bones.contains(node->mName.C_Str())) return true;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (subtreeHasBone(node->mChildren[i], bones)) return true;
    }
    return false;
}

void addJoints(const aiNode* node, int parent, const std::unordered_map<std::string, const aiBone*>& bones, Skeletons& out) {
    if (!subtreeHasBone(node, bones)) return;
    SkinJoint joint;
    joint.name = node->mName.C_Str();
    joint.parent = parent;
    aiVector3D scale, position;
    aiQuaternion rotation;
    node->mTransformation.Decompose(scale, rotation, position);
    joint.position = glm::vec3(position.x, position.y, position.z);
    joint.rotation = glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
    joint.scale = glm::vec3(scale.x, scale.y, scale.z);
    const auto bone = bones.find(joint.name);
    if (bone != bones.end()) joint.inverseBind = toGlm(bone->second->mOffsetMatrix);
    const int index = static_cast<int>(out.joints.size());
    out.byName[joint.name] = index;
    out.joints.push_back(std::move(joint));
    for (unsigned int i = 0; i < node->mNumChildren; ++i) addJoints(node->mChildren[i], index, bones, out);
}

void insertInfluence(SkinVertex& vertex, uint32_t joint, float weight) {
    size_t weakest = 0;
    for (size_t i = 1; i < kMaxSkinInfluences; ++i) {
        if (vertex.weights[i] < vertex.weights[weakest]) weakest = i;
    }
    if (weight <= vertex.weights[weakest]) return;
    vertex.joints[weakest] = joint;
    vertex.weights[weakest] = weight;
}

}

std::optional<DynamicMesh> AssetManager::importSkinned(const std::string& sourcePath) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath, kSkinnedImportFlags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        Lit::Log::Error("ASSIMP failed to load skinned model: {} with error: {}", sourcePath, importer.GetErrorString());
        return std::nullopt;
    }

    std::unordered_map<std::string, const aiBone*> bones;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) bones.emplace(mesh->mBones[b]->mName.C_Str(), mesh->mBones[b]);
    }
    Skeletons skeleton;
    if (!bones.empty()) addJoints(scene->mRootNode, -1, bones, skeleton);
    if (skeleton.joints.size() > kMaxSkinJoints) {
        Lit::Log::Error("{} has {} joints, the skinning pass supports {}", sourcePath, skeleton.joints.size(), kMaxSkinJoints);
        return std::nullopt;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    std::vector<SkinVertex> skin;
    std::vector<std::pair<const aiMesh*, size_t>> ranges;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        const size_t base = vertices.size() / kVertexFloats;
        ranges.emplace_back(mesh, base);
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            const aiVector3D p = mesh->mVertices[v];
            const aiVector3D n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0.0f, 1.0f, 0.0f);
            const aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(p.x, p.z, 0.0f);
            vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});
        }
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (unsigned int i = 0; i < 3; ++i) indices.push_back(static_cast<unsigned int>(base) + face.mIndices[i]);
        }
    }
    if (vertices.empty() || indices.empty()) {
        Lit::Log::Error("Model contains no triangles: {}", sourcePath);
        return std::nullopt;
    }

    DynamicMesh result(Mesh(std::move(vertices), std::move(indices)));
    const size_t vertexCount = result.vertexCount();
    if (!skeleton.joints.empty()) {
        skin.assign(vertexCount, SkinVertex{{0, 0, 0, 0}, {0.0f, 0.0f, 0.0f, 0.0f}});
        for (const auto& [mesh, base] : ranges) {
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                const aiBone* bone = mesh->mBones[b];
                const auto joint = skeleton.byName.find(bone->mName.C_Str());
                if (joint == skeleton.byName.end()) continue;
                for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                    const size_t vertex = base + bone->mWeights[w].mVertexId;
                    if (vertex < skin.size()) insertInfluence(skin[vertex], static_cast<uint32_t>(joint->second), bone->mWeights[w].mWeight);
                }
            }
        }
        const size_t repaired = normalizeSkinWeights(skin, static_cast<uint32_t>(skeleton.joints.size()));
        if (repaired != 0) Lit::Log::Info("{}: normalised weights of {} vertices", sourcePath, repaired);
        result.skin = std::move(skin);
        result.joints = std::move(skeleton.joints);
    }

    std::unordered_map<std::string, size_t> morphIndex;
    for (const auto& [mesh, base] : ranges) {
        for (unsigned int a = 0; a < mesh->mNumAnimMeshes && result.morphs.size() < kMaxMorphTargets; ++a) {
            const aiAnimMesh* target = mesh->mAnimMeshes[a];
            std::string name = target->mName.length ? target->mName.C_Str() : "morph" + std::to_string(a);
            auto [slot, inserted] = morphIndex.try_emplace(name, result.morphs.size());
            if (inserted) {
                MorphTarget fresh;
                fresh.name = name;
                fresh.positionDelta.assign(vertexCount, glm::vec3(0.0f));
                result.morphs.push_back(std::move(fresh));
            }
            MorphTarget& morph = result.morphs[slot->second];
            for (unsigned int v = 0; v < mesh->mNumVertices && v < target->mNumVertices; ++v) {
                const aiVector3D d = target->mVertices[v] - mesh->mVertices[v];
                morph.positionDelta[base + v] = glm::vec3(d.x, d.y, d.z);
            }
        }
    }

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* animation = scene->mAnimations[a];
        const float tps = animation->mTicksPerSecond > 0.0 ? static_cast<float>(animation->mTicksPerSecond) : kDefaultTicksPerSecond;
        SkinClip clip;
        clip.name = animation->mName.length ? animation->mName.C_Str() : "clip" + std::to_string(a);
        clip.duration = static_cast<float>(animation->mDuration) / tps;
        for (unsigned int c = 0; c < animation->mNumChannels; ++c) {
            const aiNodeAnim* channel = animation->mChannels[c];
            const int joint = result.findJoint(channel->mNodeName.C_Str());
            if (joint < 0) continue;
            SkinClipChannel out;
            out.joint = joint;
            for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
                const auto& key = channel->mPositionKeys[k];
                out.position.emplace_back(static_cast<float>(key.mTime) / tps, glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z));
            }
            for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
                const auto& key = channel->mRotationKeys[k];
                out.rotation.emplace_back(static_cast<float>(key.mTime) / tps, glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z));
            }
            for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
                const auto& key = channel->mScalingKeys[k];
                out.scale.emplace_back(static_cast<float>(key.mTime) / tps, glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z));
            }
            clip.channels.push_back(std::move(out));
        }
        if (!clip.channels.empty()) result.clips.push_back(std::move(clip));
    }

    std::string error;
    if (!validateDynamicMesh(result, &error)) {
        Lit::Log::Error("Skinned model {} is invalid: {}", sourcePath, error);
        return std::nullopt;
    }
    return result;
}
