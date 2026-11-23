#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.mesh;
import Engine.World;
import Engine.glm;
import Engine.asset;
import Engine.Render.material;
import Engine.Terrain;
import Engine.Streaming;

namespace {
constexpr uint32_t kNoLayer = 31;
}

uint32_t Engine::uploadTerrainMesh(const Mesh& mesh) { return m_renderer.initialized() ? m_renderer.uploadTerrainMesh(mesh) : 0u; }

void Engine::releaseMesh(uint32_t id) {
    if (m_renderer.initialized()) m_renderer.releaseMesh(id);
    m_meshBoxes.erase(id);
}

TerrainStreamer& Engine::enableTerrain(const TerrainSetup& setup) {
    const uint32_t shader = loadShader("Terrain", setup.shaderPath);
    const int maxLevel = std::clamp(setup.stream.lod.maxLevel, 0, 20);

    // Layer textures (5 bits per layer id, 31 = procedural).
    const auto pack = [&](const std::array<std::string, 4>& paths, bool srgb) {
        uint32_t packed = 0;
        for (uint32_t layer = 0; layer < 4; ++layer) {
            uint32_t id = kNoLayer;
            if (!paths[layer].empty()) {
                const int loaded = loadTexture(paths[layer], srgb);
                if (loaded >= 0 && loaded < static_cast<int>(kNoLayer)) id = static_cast<uint32_t>(loaded);
            }
            packed |= id << (layer * 5u);
        }
        return static_cast<float>(packed);
    };
    const float albedoPacked = pack(setup.albedoTextures, true);
    const float normalPacked = pack(setup.normalTextures, false);

    auto materials = std::make_shared<std::vector<uint32_t>>();
    for (int level = 0; level <= maxLevel; ++level) {
        Material material;
        material.name = "Terrain L" + std::to_string(level);
        material.baseColor = glm::vec3(0.5f);
        material.roughness = 0.9f;
        const float size = setup.terrain.tileSize * static_cast<float>(1u << static_cast<unsigned>(level));
        material.custom0 = glm::vec4(level < maxLevel ? size : -size, setup.stream.lod.splitFactor, setup.stream.lod.morphStart, setup.terrain.uvScale);
        material.custom1 = glm::vec4(setup.terrain.splat.sandMaxHeight, setup.terrain.splat.snowMinHeight, albedoPacked, normalPacked);
        materials->push_back(createMaterial(material));
    }

    TerrainLoaderConfig loader;
    loader.terrain = setup.terrain;
    loader.scatter = setup.scatter;
    loader.uploadMesh = [this](const Mesh& mesh) { return uploadTerrainMesh(mesh); };
    loader.uploadScatterMesh = [this](PreparedMesh& prepared) -> uint32_t {
        if (!m_renderer.initialized()) return 0u;
        ProcessedMesh processed{std::move(prepared.base), std::move(prepared.lods), std::move(prepared.clusters)};
        return uploadProcessedMesh(processed);
    };
    loader.releaseMesh = [this](uint32_t id) { releaseMesh(id); };
    loader.materialOfLevel = [materials](int level) -> uint32_t { return (*materials)[static_cast<size_t>(std::clamp(level, 0, static_cast<int>(materials->size()) - 1))]; };
    loader.shader = shader;
    loader.layer = setup.layer;
    loader.colliderMaxLevel = setup.colliderMaxLevel;
    loader.scatterMaxLevel = setup.scatterMaxLevel;
    loader.scatterClusters = setup.scatterClusters;
    loader.deltas = m_deltas.get();
    loader.meshOptions = setup.mesh;
    loader.meshOptions.packMorph = true;

    TerrainStreamConfig stream = setup.stream;
    if (m_streaming) {
        stream.commitMillisPerFrame = std::max(stream.commitMillisPerFrame, m_streaming->config().commitMillisPerFrame);
        stream.uploadBytesPerFrame = std::max(stream.uploadBytesPerFrame, m_streaming->config().uploadBytesPerFrame);
    }
    m_terrain = std::make_unique<TerrainStreamer>(m_world, std::move(loader), stream);
    m_terrain->setSharedBudget([this]() -> StreamBudget* { return m_streaming ? &m_streaming->sharedBudget() : nullptr; });

    const auto frame = [this](bool prime) {
        Camera& camera = m_world.camera();
        m_world.syncCamera();
        const glm::mat4 view = camera.getViewMatrix();
        const glm::mat4 viewProjection = camera.getProjectionMatrix() * view;
        auto planes = frustumPlanes(viewProjection);
        // Local space planes to absolute world space (local = world - origin), far plane off: the view distance bounds the terrain.
        const glm::vec3 origin(m_world.origin());
        for (glm::vec4& plane : planes) plane.w -= glm::dot(glm::vec3(plane), origin);
        planes[5] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0e9f);
        const glm::vec3 forward(-view[0][2], -view[1][2], -view[2][2]);
        const glm::vec3 cameraWorld(m_world.toWorld(camera.getPosition()));
        if (prime) m_terrain->prime(cameraWorld, forward, &planes);
        else m_terrain->update(cameraWorld, forward, &planes);
    };
    if (setup.prime) frame(true);
    addSystem(Phase::PostUpdate, "terrain", [frame](World&, float) { frame(false); }, true);
    return *m_terrain;
}
