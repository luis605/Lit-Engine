import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import std;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init() { m_renderer.init(); }

void Engine::update(const SceneDatabase& sceneDatabase, Camera& camera, const std::optional<Mesh>& mesh) { m_renderer.drawScene(sceneDatabase, camera, mesh); }

void Engine::cleanup() { m_renderer.cleanup(); }