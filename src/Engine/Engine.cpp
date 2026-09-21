struct GLFWwindow;
#include <string>
#include <vector>
#include <cstdint>

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.glm;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init(GLFWwindow* window, const int windowWidth, const int windowHeight) { m_renderer.init(window, windowWidth, windowHeight); }

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) {
    m_renderer.drawScene(sceneDatabase, camera);
}

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