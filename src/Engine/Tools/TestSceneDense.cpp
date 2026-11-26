module;

#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "Engine/Tools/TestSceneShared.hpp"

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.mesh;
import Engine.asset;

uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material);
uint32_t testSceneMesh(Engine& engine, const std::string& key, const std::function<uint32_t()>& upload);

namespace {

Mesh makeRock(int subdivisions, float roughness, float seed) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<std::array<float, 3>> points = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<uint32_t> faces = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    auto normalize = [](std::array<float, 3>& p) {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& c : p) c /= length;
    };
    for (auto& p : points) normalize(p);
    for (int level = 0; level < subdivisions; ++level) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::minmax(a, b);
            if (auto it = midpoints.find(key); it != midpoints.end()) return it->second;
            std::array<float, 3> m{points[a][0] + points[b][0], points[a][1] + points[b][1], points[a][2] + points[b][2]};
            normalize(m);
            points.push_back(m);
            return midpoints[key] = static_cast<uint32_t>(points.size() - 1);
        };
        std::vector<uint32_t> next;
        next.reserve(faces.size() * 4);
        for (size_t f = 0; f < faces.size(); f += 3) {
            const uint32_t a = faces[f], b = faces[f + 1], c = faces[f + 2];
            const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        faces = std::move(next);
    }
    auto height = [&](const std::array<float, 3>& p) {
        float h = 0.0f;
        float amplitude = roughness;
        float frequency = 2.0f;
        for (int octave = 0; octave < 6; ++octave) {
            h += amplitude * std::sin(frequency * p[0] + seed) * std::sin(frequency * 1.3f * p[1] + seed * 2.1f) * std::sin(frequency * 0.9f * p[2] + seed * 0.7f);
            amplitude *= 0.5f;
            frequency *= 2.1f;
        }
        return 1.0f + h;
    };
    std::vector<std::array<float, 3>> displaced(points.size());
    for (size_t v = 0; v < points.size(); ++v) {
        const float h = height(points[v]);
        displaced[v] = {points[v][0] * h, points[v][1] * h * 0.8f, points[v][2] * h};
    }
    std::vector<std::array<float, 3>> normals(points.size(), {0.0f, 0.0f, 0.0f});
    for (size_t f = 0; f < faces.size(); f += 3) {
        const auto& a = displaced[faces[f]];
        const auto& b = displaced[faces[f + 1]];
        const auto& c = displaced[faces[f + 2]];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (int corner = 0; corner < 3; ++corner) {
            for (int axis = 0; axis < 3; ++axis) normals[faces[f + corner]][axis] += n[axis];
        }
    }
    std::vector<float> vertices;
    vertices.reserve(points.size() * kVertexFloats);
    for (size_t v = 0; v < points.size(); ++v) {
        normalize(normals[v]);
        vertices.insert(vertices.end(), {displaced[v][0], displaced[v][1], displaced[v][2], normals[v][0], normals[v][1], normals[v][2], 0.5f + points[v][0] * 0.5f, 0.5f + points[v][2] * 0.5f});
    }
    return Mesh(std::move(vertices), std::vector<unsigned int>(faces.begin(), faces.end()));
}

}

uint32_t testSceneRockMesh(Engine& engine, const TestSceneKit& kit, int subdivisions, float roughness, float seed) {
    if (engine.headless()) return kit.cube;
    const std::string key = "rock." + std::to_string(subdivisions) + "." + std::to_string(roughness) + "." + std::to_string(seed);
    return testSceneMesh(engine, key, [&] { return engine.uploadProcessedMesh(AssetManager::processMesh(makeRock(subdivisions, roughness, seed))); });
}

uint32_t testSceneProceduralMesh(Engine& engine, const std::string& key, const std::vector<float>& vertices, const std::vector<unsigned int>& indices) {
    if (engine.headless() || vertices.empty() || indices.empty()) return 0u;
    return testSceneMesh(engine, key, [&] { return engine.uploadProcessedMesh(AssetManager::processMesh(Mesh(vertices, indices))); });
}

bool buildDenseTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    World& world = engine.world();
    const int grid = std::max(1, params.denseGrid);
    const int bigDetail = std::max(1, params.denseDetail);
    const int smallDetail = std::max(1, params.denseDetail - 1);
    const uint32_t meshes[2] = {testSceneRockMesh(engine, kit, bigDetail, 0.22f, 1.0f), testSceneRockMesh(engine, kit, smallDetail, 0.35f, 4.0f)};
    const uint64_t triangles[2] = {20ull << (2 * bigDetail), 20ull << (2 * smallDetail)};
    Material stone;
    stone.baseColor = glm::vec3(0.55f, 0.5f, 0.45f);
    stone.roughness = 0.8f;
    Material moss = stone;
    moss.baseColor = glm::vec3(0.3f, 0.45f, 0.25f);
    const uint32_t materials[2] = {testSceneMaterial(engine, kit, stone), testSceneMaterial(engine, kit, moss)};
    Material ground;
    ground.baseColor = glm::vec3(0.35f);
    ground.roughness = 0.9f;
    EntityDesc floor;
    floor.name = "ground";
    floor.mesh = kit.cube;
    floor.material = testSceneMaterial(engine, kit, ground);
    floor.position = glm::vec3(-20.0f, -1.0f, -20.0f);
    floor.scale = glm::vec3(grid * 6.0f + 40.0f, 0.2f, grid * 6.0f + 40.0f);
    world.create(floor);

    uint64_t total = 0;
    for (int z = 0; z < grid; ++z) {
        for (int x = 0; x < grid; ++x) {
            const int kind = (x * 7 + z * 3) % 5 == 0 ? 0 : 1;
            const float jitter = std::sin(static_cast<float>(x * 12.9898f + z * 78.233f)) * 43758.5453f;
            const float fraction = jitter - std::floor(jitter);
            EntityDesc rock;
            rock.name = "rock";
            rock.mesh = meshes[kind];
            rock.material = materials[(x + z) % 2];
            rock.position = glm::vec3(x * 6.0f + fraction * 2.0f, 0.5f, z * 6.0f - fraction * 1.5f);
            rock.scale = glm::vec3(kind == 0 ? 2.6f : 1.4f + fraction);
            world.create(rock);
            total += triangles[kind];
        }
    }
    Lit::Log::Info("Dense scene: {} instances, {} source triangles", grid * grid, total);
    info.start = glm::vec3(-6.0f, 4.0f, -6.0f);
    info.yaw = 45.0f;
    info.pitch = -12.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 1000.0f;
    info.suggestedTicks = 60;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer, grid, total]() {
        TestSceneMetrics metrics;
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("rocks", static_cast<double>(grid) * grid);
        metrics.set("source_triangles", static_cast<double>(total));
        return metrics;
    };
    return true;
}
