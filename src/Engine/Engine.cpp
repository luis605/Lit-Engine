struct GLFWwindow;
#include <string>

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.glm;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init(GLFWwindow* window, const int windowWidth, const int windowHeight) { m_renderer.init(window, windowWidth, windowHeight); }

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) {
    m_renderer.drawScene(sceneDatabase, camera);
}

void Engine::cleanup() { m_renderer.cleanup(); }

void Engine::uploadMesh(const Mesh& mesh) { m_renderer.uploadMesh(mesh); }

void Engine::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_renderer.AddText(text, x, y, scale, color);
}

void Engine::setSmallObjectThreshold(float threshold) { m_renderer.setSmallObjectThreshold(threshold); }
void Engine::setLargeObjectThreshold(float threshold) { m_renderer.setLargeObjectThreshold(threshold); }
void Engine::setDebugMipLevel(int mipLevel) { m_renderer.setDebugMipLevel(mipLevel); }