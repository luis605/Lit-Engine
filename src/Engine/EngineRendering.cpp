#include <GLFW/glfw3.h>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <charconv>
#include <filesystem>
#include <ostream>
#include <string_view>
#include <system_error>
#include <type_traits>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.CameraRig;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.Render.entity;
import Engine.glm;
import Engine.asset;
import Engine.input;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.reflection;
import Engine.Render.custom;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Json;
import Engine.Dump;
import Engine.Rng;
import Engine.Save;
import Engine.Streaming;
import Engine.Storylet;
import Engine.Observation;
import Engine.Delta;
import Engine.State;
import Engine.Portal;
import Engine.Portal.view;
import Engine.PortalBridge;
import Engine.Render.observation;

namespace {
void growMeshBox(const Mesh& mesh, glm::vec3& low, glm::vec3& high) {
    for (size_t offset = 0; offset + 2 < mesh.vertices.size(); offset += kVertexFloats) {
        const glm::vec3 position(mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2]);
        low = glm::min(low, position);
        high = glm::max(high, position);
    }
}
}

void Engine::recordMeshBox(uint32_t id, const Mesh& mesh) {
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    growMeshBox(mesh, low, high);
    if (low.x <= high.x) m_meshBoxes[id] = MeshLocalBox{(low + high) * 0.5f, (high - low) * 0.5f};
}

uint32_t Engine::uploadMesh(const Mesh& mesh) {
    const uint32_t id = m_renderer.uploadMesh(mesh);
    recordMeshBox(id, mesh);
    return id;
}

uint32_t Engine::uploadProcessedMesh(const Mesh& optimizedMesh, const std::vector<Mesh>& lodChain) {
    const uint32_t id = m_renderer.uploadProcessedMesh(optimizedMesh, lodChain);
    recordMeshBox(id, optimizedMesh);
    return id;
}

void Engine::refreshReflectionScene() {
    constexpr uint64_t kBoxRefreshInterval = 90;
    const RenderSettings& settings = m_renderer.settings();
    const ReflectionPlan plan = reflectionPlan(settings);
    if (!plan.enabled()) return;
    collectReflectionVolumes(m_world, m_reflectionScene);
    const bool wantsBoxes = plan.has(kReflectionModeProbes) || plan.has(kReflectionModeWorld);
    if (wantsBoxes && (m_reflectionFrame % kBoxRefreshInterval == 0)) {
        ReflectionBoxSource source;
        source.meshBox = [this](uint32_t mesh, glm::vec3& center, glm::vec3& half) {
            const auto found = m_meshBoxes.find(mesh);
            if (found == m_meshBoxes.end()) return false;
            center = found->second.center;
            half = found->second.halfExtents;
            return true;
        };
        source.materialColor = [this](uint32_t material) { return m_materials[material < m_materials.size() ? material : 0].baseColor; };
        collectReflectionBoxes(m_world, source, m_reflectionScene);
    }
    ++m_reflectionFrame;
    m_renderer.setReflectionScene(m_reflectionScene);
}

void Engine::setLodBias(float bias) { m_renderer.setLodBias(bias); }
void Engine::setForcedLod(int lod) { m_renderer.setForcedLod(lod); }
int Engine::getForcedLod() const { return m_renderer.getForcedLod(); }

void Engine::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_renderer.AddText(text, x, y, scale, color);
}

void Engine::setSmallObjectThreshold(float threshold) { m_renderer.setSmallObjectThreshold(threshold); }
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
void Engine::debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool overlay) { m_renderer.addDebugLine(from, to, color, overlay); }

void Engine::debugBox(const glm::vec3& c, const glm::vec3& h, const glm::vec4& color, bool overlay) {
    const glm::vec3 corners[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z}};
    constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) debugLine(corners[e[0]], corners[e[1]], color, overlay);
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
    Material m;
    m.name = "Material " + std::to_string(m_materials.size());
    m.baseColor = color * std::clamp(strength, 0.0f, 1.0f) + glm::vec3(0.8f) * (1.0f - std::clamp(strength, 0.0f, 1.0f));
    return createMaterial(m);
}

void Engine::updateMaterial(uint32_t id, const glm::vec3& color, float strength) {
    if (id >= m_materials.size()) return;
    Material m = m_materials[id];
    m.baseColor = color * std::clamp(strength, 0.0f, 1.0f) + glm::vec3(0.8f) * (1.0f - std::clamp(strength, 0.0f, 1.0f));
    setMaterial(id, m);
}

uint32_t Engine::createMaterial(const Material& material) {
    constexpr uint32_t maxMaterials = 1024;
    if (m_materials.size() >= maxMaterials) return 0;
    const auto id = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(material);
    uploadMaterial(id);
    return id;
}

void Engine::setMaterial(uint32_t id, const Material& material) {
    if (id >= m_materials.size()) return;
    m_materials[id] = material;
    uploadMaterial(id);
}

const Material& Engine::material(uint32_t id) const { return m_materials[id < m_materials.size() ? id : 0]; }

void Engine::uploadMaterial(uint32_t id) {
    const Material& m = m_materials[id];
    m_renderer.setMaterial(id, m);
}

uint32_t Engine::loadShader(const std::string& name, const std::string& path) {
    const std::string resolved = path.empty() ? "resources/shaders/surfaces/" + name + ".surface" : path;
    return m_renderer.loadSurfaceShader(name, resolved);
}

Ray Engine::screenRay(float screenX, float screenY) {
    m_world.syncCamera();
    return m_world.screenRay(screenX, screenY, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
}

std::optional<glm::vec2> Engine::worldToScreen(const glm::vec3& point) {
    m_world.syncCamera();
    const glm::vec4 clip = m_world.camera().getProjectionMatrix() * m_world.camera().getViewMatrix() * glm::vec4(point, 1.0f);
    if (clip.w <= 1.0e-5f) return std::nullopt;
    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(m_windowWidth), (0.5f - ndc.y * 0.5f) * static_cast<float>(m_windowHeight));
}

