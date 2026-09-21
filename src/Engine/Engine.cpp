struct GLFWwindow;
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.glm;
import Engine.asset;

Engine::Engine() {
    m_world.setMeshHooks(
        [this](uint32_t id) {
            const auto it = m_meshNames.find(id);
            return it == m_meshNames.end() ? std::string() : it->second;
        },
        [this](const std::string& name) { return loadMesh(name); });
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

void Engine::init(GLFWwindow* window, const int windowWidth, const int windowHeight) { m_renderer.init(window, windowWidth, windowHeight); }

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) {
    m_renderer.drawScene(sceneDatabase, camera);
}

void Engine::tick(float deltaTime) { m_world.update(deltaTime); }

void Engine::update() { m_renderer.drawScene(m_world.database(), m_world.camera()); }

void Engine::update(World& world) { m_renderer.drawScene(world.database(), world.camera()); }

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
}

void Engine::setAnimation(float time, uint32_t movingCount, uint32_t entityOffset) {
    m_renderer.setAnimation(time, movingCount, entityOffset);
}