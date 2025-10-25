import engine;
import renderer;
import camera;
import scene;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init() { m_renderer.init(); }

void Engine::update(const Scene& scene, Camera& camera) { m_renderer.drawScene(scene, camera); }

void Engine::cleanup() { m_renderer.cleanup(); }