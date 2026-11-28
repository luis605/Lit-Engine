#pragma once

namespace orbrun {

struct MeshData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};

void appendLathe(MeshData& mesh, const std::vector<glm::vec2>& profile, int sides, float wobble);
void appendRoundedBox(MeshData& mesh, const glm::vec3& center, const glm::vec3& half, float radius, float roughen);
void appendMesh(MeshData& target, const MeshData& source, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
void appendChunked(std::vector<MeshData>& chunks, const MeshData& source, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
void appendCrystal(MeshData& mesh, const glm::vec3& base, const glm::vec3& axis, float radius, float length, int sides, float tipFraction);
[[nodiscard]] MeshData roundedBoxMesh(const glm::vec3& half, float radius, float roughen);
[[nodiscard]] MeshData crateFrameMesh(float half);
[[nodiscard]] MeshData flameMesh();
[[nodiscard]] MeshData boulderMesh();
[[nodiscard]] MeshData grassTuftMesh();
[[nodiscard]] MeshData moltenStripMesh(float length, float width);
[[nodiscard]] float scatterUnit(uint32_t seed);

}
