module;

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module Engine.Skin;

import Engine.glm;
import Engine.mesh;

// CPU side of GPU-deformed meshes: skin weights, morph targets, bone palette, conservative bounds and a
// bit-compatible CPU reference of the skin/morph/deform compute pass (see docs/Skinning.md).

export inline constexpr uint32_t kMaxSkinInfluences = 4;
export inline constexpr uint32_t kMaxSkinJoints = 256;
export inline constexpr uint32_t kMaxMorphTargets = 64;

export struct SkinVertex {
    std::array<uint32_t, 4> joints{0, 0, 0, 0};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
};

export struct MorphTarget {
    std::string name;
    std::vector<glm::vec3> positionDelta;
    std::vector<glm::vec3> normalDelta;
};

export struct SkinJoint {
    std::string name;
    int parent = -1;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::mat4 inverseBind{1.0f};
};

export struct SkinClipChannel {
    int joint = -1;
    std::vector<std::pair<float, glm::vec3>> position;
    std::vector<std::pair<float, glm::quat>> rotation;
    std::vector<std::pair<float, glm::vec3>> scale;
};

export struct SkinClip {
    std::string name;
    float duration = 0.0f;
    std::vector<SkinClipChannel> channels;
};

export struct DynamicMesh {
    Mesh rest;
    std::vector<SkinVertex> skin;
    std::vector<SkinJoint> joints;
    std::vector<MorphTarget> morphs;
    std::vector<SkinClip> clips;

    DynamicMesh() : rest(std::vector<float>{}, std::vector<unsigned int>{}) {}
    explicit DynamicMesh(Mesh mesh) : rest(std::move(mesh)) {}
    [[nodiscard]] size_t vertexCount() const { return rest.vertices.size() / kVertexFloats; }
    [[nodiscard]] bool skinned() const { return !skin.empty() && !joints.empty(); }
    [[nodiscard]] int findMorph(const std::string& name) const;
    [[nodiscard]] int findJoint(const std::string& name) const;
};

export enum class DeformMode : uint32_t { None = 0, Wave = 1, Cloth = 2, User = 3 };

// Procedural deformation applied after morph and skin. Wave: travelling sine displacement along the normal
// and a lateral sway. Cloth: same ripple whose amplitude grows with distance below `pinY` (a hanging cloth
// pinned at pinY), with a gravity sag. User: runs `userShader` (a compute shader, see docs/Skinning.md).
export struct DeformSettings {
    DeformMode mode = DeformMode::None;
    float time = 0.0f;
    float amplitude = 0.1f;
    float frequency = 3.0f;
    float speed = 2.0f;
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
    float pinY = 0.0f;
    float pinRange = 1.0f;
    bool recomputeNormals = true;
    float userBounds = 0.0f;
    std::array<glm::vec4, 4> user{};
    std::string userShader;
};

export struct DynamicMeshStats {
    uint32_t instances = 0;
    uint32_t dispatches = 0;
    uint32_t vertices = 0;
};

export struct SkinBounds {
    std::vector<glm::vec3> boneMin;
    std::vector<glm::vec3> boneMax;
    std::vector<uint8_t> boneUsed;
    glm::vec3 restMin{0.0f};
    glm::vec3 restMax{0.0f};
    std::vector<float> morphReach;
};

export [[nodiscard]] size_t normalizeSkinWeights(std::vector<SkinVertex>& skin, uint32_t jointCount);
export [[nodiscard]] bool validateDynamicMesh(const DynamicMesh& mesh, std::string* error = nullptr);
export void buildBonePalette(std::span<const glm::mat4> jointModel, std::span<const SkinJoint> joints, std::vector<glm::mat4>& palette);
export [[nodiscard]] SkinBounds buildSkinBounds(const DynamicMesh& mesh);
// Conservative bounding sphere (xyz centre, w radius) in mesh-local space for the posed, morphed and deformed mesh.
export [[nodiscard]] glm::vec4 computeDynamicBounds(const SkinBounds& bounds, std::span<const glm::mat4> palette, std::span<const float> morphWeights, const DeformSettings& deform);
export [[nodiscard]] float deformReach(const DeformSettings& deform);

// CPU reference of resources/shaders/skin.comp + deform.comp (+ normals.comp when recomputeNormals).
export void deformMeshCpu(const DynamicMesh& mesh, std::span<const glm::mat4> palette, std::span<const float> morphWeights, const DeformSettings& deform, std::vector<float>& outVertices);
export void recomputeNormals(std::vector<float>& vertices, const std::vector<unsigned int>& indices);
export [[nodiscard]] glm::vec3 deformPoint(const DeformSettings& deform, const glm::vec3& position, const glm::vec3& normal);

export struct CharacterOptions {
    float height = 1.8f;
    int radialSegments = 12;
    int ringsPerBone = 4;
    bool headMorphs = true;
};
// Procedural skinned humanoid: 16 joints, smooth linear-blend weights, ARKit-named head morph targets.
export [[nodiscard]] DynamicMesh makeProceduralCharacter(const CharacterOptions& options = {});
// Hanging cloth strip (a flag) as a plain (unskinned) deformable mesh in the XY plane, pinned at its top edge y = 0.
export [[nodiscard]] DynamicMesh makeClothSheet(float width, float height, int columns, int rows);
