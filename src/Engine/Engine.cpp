struct GLFWwindow;
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.asset;

Engine::Engine() {
    m_world.setMeshHooks(
        [this](uint32_t id) {
            const auto it = m_meshNames.find(id);
            return it == m_meshNames.end() ? std::string() : it->second;
        },
        [this](const std::string& name) { return loadMesh(name); });
    m_world.setMeshBoundsHook([this](uint32_t id) { return m_renderer.getMeshBounds(id); });
}

uint32_t Engine::loadMesh(const std::string& name) {
    if (const auto it = m_meshIds.find(name); it != m_meshIds.end()) return it->second;

    const std::string source = "resources/models/" + name + ".obj";
    const std::string asset = "resources/assets/" + name + ".asset";
    std::filesystem::create_directories("resources/assets");
    if (!AssetManager::bake(source, asset)) { Lit::Log::Warn("Failed to bake {}", source); }
    auto mesh = AssetManager::load(asset);
    if (!mesh) {
        Lit::Log::Warn("Failed to load {}", asset);
        return 0;
    }
    const uint32_t id = uploadMesh(*mesh);
    m_meshIds[name] = id;
    m_meshNames[id] = name;
    return id;
}

Engine::~Engine() {}

void Engine::init(GLFWwindow* window, const int windowWidth, const int windowHeight) {
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
    m_world.camera().updateAspectRatio(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    m_renderer.init(window, windowWidth, windowHeight);
}

std::optional<RayHit> Engine::pick(float screenX, float screenY) {
    m_world.syncCamera();
    const Ray ray = m_world.screenRay(screenX, screenY, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
    return m_world.raycast(ray.origin, ray.direction);
}

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) {
    m_renderer.drawScene(sceneDatabase, camera);
}

void Engine::tick(float deltaTime) {
    constexpr float maxFrame = 0.25f;
    TimeState& t = m_world.timeState();
    t.unscaledDeltaTime = std::min(deltaTime, maxFrame);
    t.deltaTime = t.paused ? 0.0f : t.unscaledDeltaTime * t.timeScale;
    t.unscaledElapsed += t.unscaledDeltaTime;
    t.elapsed += t.deltaTime;
    ++t.frame;

    if (!t.paused) {
        m_accumulator += t.deltaTime;
        while (m_accumulator >= m_fixedStep) {
            m_world.fixedUpdate(m_fixedStep);
            m_accumulator -= m_fixedStep;
        }
        m_world.update(t.deltaTime);
    }
    m_world.events().dispatch();
}

void Engine::update() {
    m_world.syncCamera();
    m_renderer.drawScene(m_world.database(), m_world.camera());
}

void Engine::update(World& world) {
    world.syncCamera();
    m_renderer.drawScene(world.database(), world.camera());
}

void Engine::present() { m_renderer.present(); }

void Engine::cleanup() { m_renderer.cleanup(); }

uint32_t Engine::uploadMesh(const Mesh& mesh) { return m_renderer.uploadMesh(mesh); }

void Engine::setLodBias(float bias) { m_renderer.setLodBias(bias); }
void Engine::setForcedLod(int lod) { m_renderer.setForcedLod(lod); }
int Engine::getForcedLod() const { return m_renderer.getForcedLod(); }

void Engine::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_renderer.AddText(text, x, y, scale, color);
}

void Engine::setSmallObjectThreshold(float threshold) { m_renderer.setSmallObjectThreshold(threshold); }
void Engine::setLargeObjectThreshold(float threshold) { m_renderer.setLargeObjectThreshold(threshold); }
void Engine::setDebugDepthMode(bool enabled) { m_renderer.setDebugDepthMode(enabled); }
bool Engine::isDebugDepthMode() const { return m_renderer.isDebugDepthMode(); }
void Engine::setFullProfiling(bool enabled) { m_renderer.setFullProfiling(enabled); }

void Engine::uploadBasePositions(const std::vector<glm::vec3>& basePositions) {
    m_renderer.uploadBasePositions(basePositions);
    m_animationBase = basePositions;
    applyWorldAnimation();
}

void Engine::setAnimation(float time, uint32_t movingCount, uint32_t entityOffset) {
    m_renderer.setAnimation(time, movingCount, entityOffset);
    m_world.setAnimationTime(time);
    if (entityOffset != m_animationOffset || movingCount != m_animationCount) {
        m_animationOffset = entityOffset;
        m_animationCount = movingCount;
        applyWorldAnimation();
    }
}
void Engine::debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) { m_renderer.addDebugLine(from, to, color); }

void Engine::debugBox(const glm::vec3& c, const glm::vec3& h, const glm::vec4& color) {
    const glm::vec3 corners[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z}};
    constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) debugLine(corners[e[0]], corners[e[1]], color);
}

void Engine::debugSphere(const glm::vec3& center, float radius, const glm::vec4& color) {
    constexpr int segments = 24;
    constexpr float step = 6.28318530718f / segments;
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * i, a1 = step * (i + 1);
        const float c0 = std::cos(a0) * radius, s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius, s1 = std::sin(a1) * radius;
        debugLine(center + glm::vec3(c0, s0, 0.0f), center + glm::vec3(c1, s1, 0.0f), color);
        debugLine(center + glm::vec3(c0, 0.0f, s0), center + glm::vec3(c1, 0.0f, s1), color);
        debugLine(center + glm::vec3(0.0f, c0, s0), center + glm::vec3(0.0f, c1, s1), color);
    }
}

void Engine::debugRay(const Ray& ray, float length, const glm::vec4& color) { debugLine(ray.origin, ray.origin + ray.direction * length, color); }

void Engine::debugHierarchy(const glm::vec4& color) {
    m_world.forEach([&](EntityHandle e) {
        const EntityHandle parent = m_world.getParent(e);
        if (!parent.isNull()) debugLine(m_world.getWorldPosition(parent), m_world.getWorldPosition(e), color);
    });
}

void Engine::applyWorldAnimation() {
    const size_t count = std::min<size_t>(m_animationCount, m_animationBase.size());
    m_world.setAnimation(m_animationOffset, std::vector<glm::vec3>(m_animationBase.begin(), m_animationBase.begin() + count));
}

uint32_t Engine::createMaterial(const glm::vec3& color, float strength) {
    constexpr uint32_t maxMaterials = 1024;
    if (m_materialCount >= maxMaterials) return 0;
    const uint32_t id = m_materialCount++;
    updateMaterial(id, color, strength);
    return id;
}

void Engine::updateMaterial(uint32_t id, const glm::vec3& color, float strength) {
    m_renderer.setMaterial(id, glm::vec4(color, std::clamp(strength, 0.0f, 1.0f)));
}
