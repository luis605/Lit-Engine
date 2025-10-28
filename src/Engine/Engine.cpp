import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import std;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init() { m_renderer.init(); }

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) { m_renderer.drawScene(sceneDatabase, camera); }

void Engine::cleanup() { m_renderer.cleanup(); }

void Engine::uploadMesh(const Mesh& mesh) { m_renderer.uploadMesh(mesh); }