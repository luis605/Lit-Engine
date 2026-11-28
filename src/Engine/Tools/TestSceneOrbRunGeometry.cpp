module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

module Engine.OrbRun;

import Engine.glm;
import Engine.mesh;

#include "OrbRunGeometry.hpp"

namespace orbrun {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kBevelSegments = 3;
constexpr float kRoughCellMeters = 0.22f;
constexpr int kRoughMaxCells = 11;
constexpr float kSharpBevelRadius = 0.025f;
constexpr size_t kMaxChunkIndices = 4000 * 3;
constexpr int kFlameSides = 12;
constexpr int kBoulderRings = 20;
constexpr int kBoulderSides = 28;
constexpr int kGrassBlades = 7;
constexpr float kStripCellMeters = 0.25f;
constexpr int kStripCellsAcross = 3;

unsigned int vertexTotal(const MeshData& mesh) { return static_cast<unsigned int>(mesh.vertices.size() / kVertexFloats); }

glm::vec3 vertexAt(const MeshData& mesh, unsigned int index) {
    const size_t offset = static_cast<size_t>(index) * kVertexFloats;
    return glm::vec3(mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2]);
}

glm::vec3 normalAt(const MeshData& mesh, unsigned int index) {
    const size_t offset = static_cast<size_t>(index) * kVertexFloats + 3;
    return glm::vec3(mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2]);
}

void addVertex(MeshData& mesh, const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv) {
    mesh.vertices.insert(mesh.vertices.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z, uv.x, uv.y});
}

void addFacingTriangle(MeshData& mesh, unsigned int a, unsigned int b, unsigned int c) {
    const glm::vec3 facing = glm::cross(vertexAt(mesh, b) - vertexAt(mesh, a), vertexAt(mesh, c) - vertexAt(mesh, a));
    if (glm::dot(facing, normalAt(mesh, a) + normalAt(mesh, b) + normalAt(mesh, c)) < 0.0f) std::swap(b, c);
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

void addGrid(MeshData& mesh, unsigned int first, int columns, int rows) {
    for (int row = 0; row + 1 < rows; ++row) {
        for (int column = 0; column + 1 < columns; ++column) {
            const unsigned int a = first + static_cast<unsigned int>(row * columns + column);
            const unsigned int b = a + 1;
            const unsigned int c = a + static_cast<unsigned int>(columns);
            addFacingTriangle(mesh, a, b, c + 1);
            addFacingTriangle(mesh, a, c + 1, c);
        }
    }
}

float lumpiness(const glm::vec3& point) {
    const float broad = std::sin(point.x * 4.1f + point.y * 1.7f) * std::sin(point.y * 3.7f - point.z * 2.3f) * std::sin(point.z * 4.9f + point.x);
    const float fine = std::sin(point.x * 11.0f - point.z * 7.0f) * std::sin(point.y * 13.0f + point.x * 5.0f);
    return broad + 0.45f * fine;
}

std::vector<float> faceStops(float half, float radius, bool roughened) {
    const float flat = half - radius;
    const int segments = radius < kSharpBevelRadius ? 1 : kBevelSegments;
    std::vector<float> stops;
    for (int step = 0; step <= segments; ++step) stops.push_back(-half + radius * static_cast<float>(step) / static_cast<float>(segments));
    const int cells = roughened ? std::clamp(static_cast<int>(2.0f * flat / kRoughCellMeters), 1, kRoughMaxCells) : 1;
    for (int cell = 1; cell < cells; ++cell) stops.push_back(-flat + 2.0f * flat * static_cast<float>(cell) / static_cast<float>(cells));
    for (int step = 0; step <= segments; ++step) stops.push_back(flat + radius * static_cast<float>(step) / static_cast<float>(segments));
    return stops;
}

}

float scatterUnit(uint32_t seed) {
    uint32_t value = seed * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    value = (value >> 22u) ^ value;
    return static_cast<float>(value >> 8u) / 16777216.0f;
}

void appendLathe(MeshData& mesh, const std::vector<glm::vec2>& profile, int sides, float wobble) {
    const unsigned int first = vertexTotal(mesh);
    const size_t rings = profile.size();
    for (size_t ring = 0; ring < rings; ++ring) {
        const glm::vec2 before = profile[ring == 0 ? 0 : ring - 1];
        const glm::vec2 after = profile[std::min(ring + 1, rings - 1)];
        const glm::vec2 tangent = after - before;
        const glm::vec2 outward = glm::normalize(glm::vec2(tangent.y, -tangent.x));
        for (int side = 0; side <= sides; ++side) {
            const float angle = 2.0f * kPi * static_cast<float>(side % sides) / static_cast<float>(sides);
            const float radius = profile[ring].x * (1.0f + wobble * std::sin(3.0f * angle + profile[ring].y * 2.0f));
            const glm::vec3 normal(outward.x * std::cos(angle), outward.y, outward.x * std::sin(angle));
            addVertex(mesh, glm::vec3(radius * std::cos(angle), profile[ring].y, radius * std::sin(angle)), normal, glm::vec2(static_cast<float>(side) / static_cast<float>(sides), static_cast<float>(ring) / static_cast<float>(rings)));
        }
    }
    addGrid(mesh, first, sides + 1, static_cast<int>(rings));
}

void appendRoundedBox(MeshData& mesh, const glm::vec3& center, const glm::vec3& half, float radius, float roughen) {
    const float rounding = std::min({radius, half.x, half.y, half.z});
    const glm::vec3 core = half - glm::vec3(rounding);
    for (int axis = 0; axis < 3; ++axis) {
        const int across = (axis + 1) % 3;
        const int up = (axis + 2) % 3;
        const std::vector<float> columns = faceStops(half[across], rounding, roughen > 0.0f);
        const std::vector<float> rows = faceStops(half[up], rounding, roughen > 0.0f);
        for (const float sign : {-1.0f, 1.0f}) {
            const unsigned int first = vertexTotal(mesh);
            for (const float row : rows) {
                for (const float column : columns) {
                    glm::vec3 flat(0.0f);
                    flat[axis] = sign * half[axis];
                    flat[across] = column;
                    flat[up] = row;
                    const glm::vec3 inner = glm::clamp(flat, -core, core);
                    glm::vec3 normal = flat - inner;
                    if (glm::dot(normal, normal) < 1.0e-12f) {
                        normal = glm::vec3(0.0f);
                        normal[axis] = sign;
                    }
                    normal = glm::normalize(normal);
                    const glm::vec3 position = inner + normal * rounding;
                    const glm::vec3 displaced = position + normal * (roughen * lumpiness(center + position));
                    addVertex(mesh, center + displaced, normal, glm::vec2(column / (2.0f * half[across]) + 0.5f, row / (2.0f * half[up]) + 0.5f));
                }
            }
            addGrid(mesh, first, static_cast<int>(columns.size()), static_cast<int>(rows.size()));
        }
    }
}

void appendMesh(MeshData& target, const MeshData& source, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    const unsigned int first = vertexTotal(target);
    for (unsigned int index = 0; index < vertexTotal(source); ++index) {
        const size_t uv = static_cast<size_t>(index) * kVertexFloats + 6;
        addVertex(target, position + rotation * (vertexAt(source, index) * scale), glm::normalize(rotation * (normalAt(source, index) / scale)), glm::vec2(source.vertices[uv], source.vertices[uv + 1]));
    }
    for (const unsigned int index : source.indices) target.indices.push_back(first + index);
}

void appendChunked(std::vector<MeshData>& chunks, const MeshData& source, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    if (chunks.empty() || chunks.back().indices.size() + source.indices.size() > kMaxChunkIndices) chunks.emplace_back();
    appendMesh(chunks.back(), source, position, rotation, scale);
}

void appendCrystal(MeshData& mesh, const glm::vec3& base, const glm::vec3& axis, float radius, float length, int sides, float tipFraction) {
    const glm::vec3 along = glm::normalize(axis);
    const glm::vec3 helper = std::abs(along.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(helper, along));
    const glm::vec3 forward = glm::cross(along, right);
    const glm::vec3 shoulder = base + along * (length * (1.0f - tipFraction));
    const glm::vec3 tip = base + along * length;
    for (int side = 0; side < sides; ++side) {
        const auto radial = [&](int index) {
            const float angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(sides);
            return right * std::cos(angle) + forward * std::sin(angle);
        };
        const glm::vec3 near = radial(side);
        const glm::vec3 far = radial(side + 1);
        const glm::vec3 faceNormal = glm::normalize(near + far);
        const unsigned int first = vertexTotal(mesh);
        addVertex(mesh, base + near * radius, faceNormal, glm::vec2(0.0f, 0.0f));
        addVertex(mesh, base + far * radius, faceNormal, glm::vec2(1.0f, 0.0f));
        addVertex(mesh, shoulder + near * radius * 0.86f, faceNormal, glm::vec2(0.0f, 1.0f - tipFraction));
        addVertex(mesh, shoulder + far * radius * 0.86f, faceNormal, glm::vec2(1.0f, 1.0f - tipFraction));
        addFacingTriangle(mesh, first, first + 1, first + 3);
        addFacingTriangle(mesh, first, first + 3, first + 2);
        const glm::vec3 slope = glm::normalize(faceNormal * (length * tipFraction) + along * radius);
        const unsigned int cap = vertexTotal(mesh);
        addVertex(mesh, shoulder + near * radius * 0.86f, slope, glm::vec2(0.0f, 1.0f - tipFraction));
        addVertex(mesh, shoulder + far * radius * 0.86f, slope, glm::vec2(1.0f, 1.0f - tipFraction));
        addVertex(mesh, tip, slope, glm::vec2(0.5f, 1.0f));
        addFacingTriangle(mesh, cap, cap + 1, cap + 2);
    }
}

MeshData roundedBoxMesh(const glm::vec3& half, float radius, float roughen) {
    MeshData mesh;
    appendRoundedBox(mesh, glm::vec3(0.0f), half, radius, roughen);
    return mesh;
}

MeshData crateFrameMesh(float half) {
    MeshData mesh;
    const float beam = half * 0.13f;
    const float edge = half - beam * 0.6f;
    for (int axis = 0; axis < 3; ++axis) {
        for (const float first : {-edge, edge}) {
            for (const float second : {-edge, edge}) {
                glm::vec3 center(0.0f);
                center[(axis + 1) % 3] = first;
                center[(axis + 2) % 3] = second;
                glm::vec3 size(beam);
                size[axis] = half + beam * 0.4f;
                appendRoundedBox(mesh, center, size, beam * 0.35f, 0.0f);
            }
        }
    }
    return mesh;
}

MeshData flameMesh() {
    MeshData mesh;
    appendLathe(mesh, {{0.001f, 0.0f}, {0.2f, 0.06f}, {0.25f, 0.18f}, {0.2f, 0.36f}, {0.11f, 0.6f}, {0.04f, 0.82f}, {0.001f, 1.0f}}, kFlameSides, 0.14f);
    return mesh;
}

MeshData boulderMesh() {
    MeshData mesh;
    const auto radius = [](const glm::vec3& direction) {
        const float broad = std::sin(direction.x * 2.3f + 1.7f) * std::sin(direction.y * 2.9f + 0.4f) * std::sin(direction.z * 2.1f + 2.6f);
        const float medium = std::sin(direction.x * 5.7f + direction.z * 3.1f) * std::sin(direction.y * 6.3f - direction.x * 2.2f);
        const float fine = std::sin(direction.x * 13.0f - direction.y * 7.0f) * std::sin(direction.z * 11.0f + direction.y * 5.0f);
        return 1.0f + 0.28f * broad + 0.12f * medium + 0.045f * fine;
    };
    std::vector<glm::vec3> points;
    for (int ring = 0; ring <= kBoulderRings; ++ring) {
        const float latitude = kPi * (static_cast<float>(ring) / static_cast<float>(kBoulderRings) - 0.5f);
        for (int side = 0; side <= kBoulderSides; ++side) {
            const float longitude = 2.0f * kPi * static_cast<float>(side % kBoulderSides) / static_cast<float>(kBoulderSides);
            const glm::vec3 direction(std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude));
            points.push_back(glm::vec3(0.0f, 0.65f, 0.0f) + direction * radius(direction) * glm::vec3(1.15f, 0.75f, 1.05f));
        }
    }
    const auto at = [&](int ring, int side) { return points[static_cast<size_t>(ring * (kBoulderSides + 1) + (side + kBoulderSides) % kBoulderSides)]; };
    for (int ring = 0; ring <= kBoulderRings; ++ring) {
        for (int side = 0; side <= kBoulderSides; ++side) {
            const glm::vec3 point = points[static_cast<size_t>(ring * (kBoulderSides + 1) + side)];
            const glm::vec3 acrossRing = at(ring, side + 1) - at(ring, side - 1);
            const glm::vec3 alongSide = at(std::min(ring + 1, kBoulderRings), side) - at(std::max(ring - 1, 0), side);
            const glm::vec3 cross = glm::cross(alongSide, acrossRing);
            const bool pole = ring == 0 || ring == kBoulderRings;
            const glm::vec3 normal = pole || glm::dot(cross, cross) < 1.0e-8f ? glm::vec3(0.0f, ring == 0 ? -1.0f : 1.0f, 0.0f) : glm::normalize(cross);
            addVertex(mesh, point, normal, glm::vec2(static_cast<float>(side) / static_cast<float>(kBoulderSides), static_cast<float>(ring) / static_cast<float>(kBoulderRings)));
        }
    }
    for (int ring = 0; ring < kBoulderRings; ++ring) {
        for (int side = 0; side < kBoulderSides; ++side) {
            const unsigned int a = static_cast<unsigned int>(ring * (kBoulderSides + 1) + side);
            const unsigned int c = a + static_cast<unsigned int>(kBoulderSides + 1);
            mesh.indices.insert(mesh.indices.end(), {a, c, a + 1, a + 1, c, c + 1});
        }
    }
    return mesh;
}

MeshData grassTuftMesh() {
    MeshData mesh;
    for (int blade = 0; blade < kGrassBlades; ++blade) {
        const uint32_t seed = static_cast<uint32_t>(blade) * 11u + 3u;
        const float angle = 2.0f * kPi * (static_cast<float>(blade) + scatterUnit(seed)) / static_cast<float>(kGrassBlades);
        const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));
        const glm::vec3 side(-outward.z, 0.0f, outward.x);
        const float height = 0.22f + 0.2f * scatterUnit(seed + 1u);
        const float width = 0.022f;
        const glm::vec3 root = outward * (0.02f + 0.09f * scatterUnit(seed + 2u));
        const glm::vec3 middle = root + glm::vec3(0.0f, height * 0.65f, 0.0f) + outward * (height * 0.18f);
        const glm::vec3 tip = root + glm::vec3(0.0f, height, 0.0f) + outward * (height * 0.55f);
        const glm::vec3 lean = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) + outward * 0.35f);
        for (const float facing : {-1.0f, 1.0f}) {
            const unsigned int first = vertexTotal(mesh);
            addVertex(mesh, root - side * width, lean, glm::vec2(0.0f, 0.0f));
            addVertex(mesh, root + side * width, lean, glm::vec2(1.0f, 0.0f));
            addVertex(mesh, middle - side * (width * 0.7f), lean, glm::vec2(0.0f, 0.65f));
            addVertex(mesh, middle + side * (width * 0.7f), lean, glm::vec2(1.0f, 0.65f));
            addVertex(mesh, tip, lean, glm::vec2(0.5f, 1.0f));
            const unsigned int triangles[3][3] = {{0, 1, 3}, {0, 3, 2}, {2, 3, 4}};
            for (const auto& triangle : triangles) {
                const unsigned int a = first + triangle[0];
                unsigned int b = first + triangle[1];
                unsigned int c = first + triangle[2];
                const glm::vec3 normal = glm::cross(vertexAt(mesh, b) - vertexAt(mesh, a), vertexAt(mesh, c) - vertexAt(mesh, a));
                if (glm::dot(normal, outward) * facing < 0.0f) std::swap(b, c);
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            }
        }
    }
    return mesh;
}

MeshData moltenStripMesh(float length, float width) {
    MeshData mesh;
    const int columns = std::max(2, static_cast<int>(length / kStripCellMeters) + 1);
    const int rows = kStripCellsAcross + 1;
    for (int row = 0; row < rows; ++row) {
        const float across = static_cast<float>(row) / static_cast<float>(rows - 1);
        for (int column = 0; column < columns; ++column) {
            const float along = static_cast<float>(column) / static_cast<float>(columns - 1);
            addVertex(mesh, glm::vec3((along - 0.5f) * length, 0.0f, (across - 0.5f) * width), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(along, across));
        }
    }
    addGrid(mesh, 0, columns, rows);
    return mesh;
}

}
